#include "usb_payload_importer.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <utility>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace onion::shellui::usb_payloads {
namespace {

bool valid_name(const std::string &name) {
  if (name.empty() || name[0] == '.' || name.find('\0') != std::string::npos ||
      name.find_first_of("/\\") != std::string::npos || name.size() <= 4 ||
      name.size() > 255)
    return false;
  const std::string_view suffix(name.data() + name.size() - 4, 4);
  return (suffix[0] == '.') && (suffix[1] == 'e' || suffix[1] == 'E') &&
         (suffix[2] == 'l' || suffix[2] == 'L') &&
         (suffix[3] == 'f' || suffix[3] == 'F');
}

std::string destination_name(const std::string &name) {
  return name.substr(0, name.size() - 4) + ".elf";
}

bool is_regular(const std::string &path, struct stat *out = nullptr) {
  struct stat st {};
  if (lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
    return false;
  if (out)
    *out = st;
  return true;
}

// Takes ownership of both descriptors, including on a copy or sync failure.
ImportResult copy_payload(int source, int target, std::uint64_t expected_size) {
  ImportResult result = ImportResult::Completed;
  // mkstemp starts at 0600; the installed payload must be readable by the loader.
  if (fchmod(target, 0644) != 0)
    result = ImportResult::CannotCreateDestination;
  std::uint64_t copied = 0;
  char buffer[64 * 1024];
  while (result == ImportResult::Completed) {
    const ssize_t n = read(source, buffer, sizeof(buffer));
    if (n < 0 && errno == EINTR)
      continue;
    if (n < 0) {
      result = ImportResult::ReadError;
      break;
    }
    if (n == 0)
      break;
    copied += static_cast<std::uint64_t>(n);
    ssize_t offset = 0;
    while (offset < n) {
      const ssize_t written =
          write(target, buffer + offset, static_cast<size_t>(n - offset));
      if (written < 0 && errno == EINTR)
        continue;
      if (written <= 0) {
        result = ImportResult::WriteError;
        break;
      }
      offset += written;
    }
  }
  if (result == ImportResult::Completed && copied != expected_size)
    result = ImportResult::ReadError;
  if (result == ImportResult::Completed && fsync(target) != 0)
    result = ImportResult::WriteError;
  if (close(target) != 0 && result == ImportResult::Completed)
    result = ImportResult::WriteError;
  if (close(source) != 0 && result == ImportResult::Completed)
    result = ImportResult::ReadError;
  return result;
}

ImportResult install_new_payload(const std::string &temp_path,
                                 const std::string &final_path,
                                 std::uint64_t size) {
  const int source = open(temp_path.c_str(), O_RDONLY | O_NOFOLLOW);
  if (source < 0)
    return ImportResult::CannotOpenSource;
  // PS5 storage cannot rely on hard links. O_EXCL keeps the no-replace
  // decision atomic without a check-then-rename that could overwrite a file.
  const int target = open(final_path.c_str(),
                          O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
  if (target < 0) {
    const int error = errno;
    close(source);
    return error == EEXIST ? ImportResult::DestinationAlreadyExists
                           : ImportResult::CannotCreateDestination;
  }
  struct stat created {};
  if (fstat(target, &created) != 0) {
    close(target);
    close(source);
    unlink(final_path.c_str());
    return ImportResult::CannotCreateDestination;
  }
  const ImportResult result = copy_payload(source, target, size);
  if (result != ImportResult::Completed) {
    // Do not remove a different file published by another writer during I/O.
    struct stat current {};
    if (lstat(final_path.c_str(), &current) == 0 &&
        current.st_dev == created.st_dev && current.st_ino == created.st_ino)
      unlink(final_path.c_str());
  }
  return result;
}

} // namespace

std::vector<Entry> scan_directory(const std::string &directory,
                                  const std::string &destination,
                                  unsigned usb_index, bool *opened) {
  std::vector<Entry> entries;
  if (opened)
    *opened = false;
  if (usb_index > 7)
    return entries;
  DIR *dir = opendir(directory.c_str());
  if (!dir)
    return entries;
  if (opened)
    *opened = true;
  while (dirent *item = readdir(dir)) {
    const std::string name(item->d_name);
    if (!valid_name(name))
      continue;
    const std::string source = directory + "/" + name;
    struct stat st {};
    if (!is_regular(source, &st) || st.st_size < 0)
      continue;
    Entry entry;
    entry.name = name;
    entry.source_path = source;
    entry.size = static_cast<std::uint64_t>(st.st_size);
    entry.usb_index = usb_index;
    entry.installed =
        lstat((destination + "/" + destination_name(name)).c_str(), &st) == 0;
    entries.push_back(std::move(entry));
  }
  closedir(dir);
  std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
    return a.name < b.name;
  });
  return entries;
}

ImportResult import_payload(const Entry &entry, const std::string &destination,
                            bool replace) {
  if (!valid_name(entry.name) || entry.usb_index > 7 ||
      entry.source_path.empty() ||
      entry.source_path.find('\0') != std::string::npos ||
      entry.source_path.substr(entry.source_path.find_last_of('/') + 1) !=
          entry.name)
    return ImportResult::InvalidName;

  const std::string final_path =
      destination + "/" + destination_name(entry.name);
  const int source = open(entry.source_path.c_str(), O_RDONLY | O_NOFOLLOW);
  if (source < 0)
    return ImportResult::CannotOpenSource;
  struct stat source_stat {};
  if (fstat(source, &source_stat) != 0 || !S_ISREG(source_stat.st_mode) ||
      source_stat.st_size < 0) {
    close(source);
    return ImportResult::CannotOpenSource;
  }
  if (mkdir(destination.c_str(), 0777) != 0 && errno != EEXIST) {
    close(source);
    return ImportResult::CannotCreateDestination;
  }
  struct stat destination_stat {};
  if (lstat(destination.c_str(), &destination_stat) != 0 ||
      !S_ISDIR(destination_stat.st_mode)) {
    close(source);
    return ImportResult::CannotCreateDestination;
  }
  if (!replace && lstat(final_path.c_str(), &destination_stat) == 0) {
    close(source);
    return ImportResult::DestinationAlreadyExists;
  }
  std::string temp_path = destination + "/.usb-payload-XXXXXX";
  const int target = mkstemp(temp_path.data());
  if (target < 0) {
    close(source);
    return ImportResult::CannotCreateDestination;
  }

  const auto size = static_cast<std::uint64_t>(source_stat.st_size);
  ImportResult result = copy_payload(source, target, size);
  if (result == ImportResult::Completed) {
    if (replace) {
      if (rename(temp_path.c_str(), final_path.c_str()) != 0)
        result = ImportResult::RenameError;
    } else {
      result = install_new_payload(temp_path, final_path, size);
    }
  }
  if (result != ImportResult::Completed || !replace)
    unlink(temp_path.c_str());
  return result;
}

} // namespace onion::shellui::usb_payloads
