/* Copyright (C) 2026 OnionHEN / LightningMods */

#include <onion/sprx_catalog.hpp>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace onion::sprx {
namespace {

constexpr size_t kMaxManifestBytes = 1024 * 1024;
constexpr size_t kMaxEntries = 256;
constexpr size_t kMaxDependencies = 64;

std::string_view trim(std::string_view value) noexcept {
  size_t first = 0;
  while (first < value.size() &&
         std::isspace(static_cast<unsigned char>(value[first])))
    ++first;
  size_t last = value.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(value[last - 1])))
    --last;
  return value.substr(first, last - first);
}

bool valid_id(std::string_view id) noexcept {
  if (id.empty() || id.size() >= 32)
    return false;
  return std::all_of(id.begin(), id.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
  });
}

bool valid_title_id(std::string_view id) noexcept {
  if (id.size() != 9)
    return false;
  for (const char c : id) {
    if (!std::isalnum(static_cast<unsigned char>(c)))
      return false;
  }
  return true;
}

bool valid_title_prefix(std::string_view prefix) noexcept {
  if (prefix.empty() || prefix.size() >= 9)
    return false;
  for (const char c : prefix) {
    if (!std::isalnum(static_cast<unsigned char>(c)))
      return false;
  }
  return true;
}

bool valid_path(std::string_view path) noexcept {
  if (path.empty() || path.front() != '/')
    return false;
  size_t component = 0;
  while (component < path.size()) {
    const size_t slash = path.find('/', component);
    const size_t end = slash == std::string_view::npos ? path.size() : slash;
    if (path.substr(component, end - component) == "..")
      return false;
    component = slash == std::string_view::npos ? path.size() : slash + 1;
  }
  const size_t dot = path.find_last_of('.');
  if (dot == std::string_view::npos)
    return false;
  const std::string_view suffix = path.substr(dot);
  if (suffix.size() != 5 && suffix.size() != 4)
    return false;
  const std::string expected = suffix.size() == 5 ? ".sprx" : ".prx";
  for (size_t i = 0; i < suffix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(suffix[i])) != expected[i])
      return false;
  }
  return true;
}

bool parse_bool(std::string_view value, bool *out) noexcept {
  if (!out)
    return false;
  value = trim(value);
  if (value == "true" || value == "1") {
    *out = true;
    return true;
  }
  if (value == "false" || value == "0") {
    *out = false;
    return true;
  }
  return false;
}

bool parse_priority(std::string_view value, int32_t *out) noexcept {
  if (!out)
    return false;
  value = trim(value);
  if (value.empty())
    return false;
  int32_t parsed = 0;
  const char *begin = value.data();
  const char *end = begin + value.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end)
    return false;
  *out = parsed;
  return true;
}

bool split_csv(std::string_view value, std::vector<std::string> *out,
               bool (*validator)(std::string_view), size_t max_count) {
  if (!out)
    return false;
  out->clear();
  value = trim(value);
  if (value.empty())
    return true;
  size_t start = 0;
  while (start <= value.size()) {
    const size_t comma = value.find(',', start);
    const size_t end = comma == std::string_view::npos ? value.size() : comma;
    const std::string_view item = trim(value.substr(start, end - start));
    if (item.empty() || !validator(item) || out->size() >= max_count)
      return false;
    out->emplace_back(item);
    if (comma == std::string_view::npos)
      break;
    start = comma + 1;
  }
  return true;
}

void add_issue(std::vector<SprxCatalogIssue> *issues, size_t line,
               std::string_view id, std::string_view message) {
  if (issues)
    issues->push_back({line, std::string(id), std::string(message)});
}

bool has_key(const std::set<std::string> &keys, std::string_view key) {
  return keys.find(std::string(key)) != keys.end();
}

} // namespace

bool SprxCatalog::parse(std::string_view text,
                        std::vector<SprxCatalogIssue> *issues) {
  if (issues)
    issues->clear();
  entries_.clear();
  if (text.size() > kMaxManifestBytes) {
    add_issue(issues, 0, {}, "manifest exceeds size limit");
    return false;
  }

  std::vector<SprxManifestEntry> parsed;
  std::set<std::string> ids;
  SprxManifestEntry current;
  std::set<std::string> keys;
  std::string current_id;
  size_t section_line = 0;
  bool section_open = false;
  bool ok = true;

  const auto finish = [&]() {
    if (!section_open)
      return;
    if (!has_key(keys, "path") || current.path.empty()) {
      add_issue(issues, section_line, current_id, "path is required");
      ok = false;
    }
    if (!valid_path(current.path)) {
      add_issue(issues, section_line, current_id,
                "path must be absolute and end in .sprx or .prx");
      ok = false;
    }
    if (current.exact_title_ids.empty() && current.title_id_prefixes.empty()) {
      add_issue(issues, section_line, current_id,
                "at least one title ID or title ID prefix is required");
      ok = false;
    }
    if (current.dependencies.size() > kMaxDependencies) {
      add_issue(issues, section_line, current_id, "too many dependencies");
      ok = false;
    }
    std::set<std::string> dependency_ids;
    for (const std::string &dependency : current.dependencies) {
      if (!dependency_ids.insert(dependency).second) {
        add_issue(issues, section_line, current_id,
                  "duplicate dependency");
        ok = false;
        break;
      }
    }
    if (!ids.insert(current.id).second) {
      add_issue(issues, section_line, current_id, "duplicate plugin ID");
      ok = false;
    }
    parsed.push_back(std::move(current));
    current = {};
    keys.clear();
  };

  size_t line_number = 1;
  size_t cursor = 0;
  while (cursor <= text.size()) {
    const size_t newline = text.find('\n', cursor);
    const size_t end = newline == std::string_view::npos ? text.size() : newline;
    std::string_view line = trim(text.substr(cursor, end - cursor));
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1);
    if (line.empty() || line.front() == '#' || line.front() == ';') {
      // comment/blank
    } else if (line.front() == '[' && line.back() == ']') {
      finish();
      const std::string_view section = trim(line.substr(1, line.size() - 2));
      constexpr std::string_view prefix = "plugin.";
      if (section.size() <= prefix.size() ||
          section.substr(0, prefix.size()) != prefix ||
          !valid_id(section.substr(prefix.size())) || parsed.size() >= kMaxEntries) {
        add_issue(issues, line_number, {}, "invalid plugin section");
        ok = false;
        section_open = false;
      } else {
        current_id = std::string(section.substr(prefix.size()));
        current.id = current_id;
        section_line = line_number;
        section_open = true;
      }
    } else {
      const size_t equal = line.find('=');
      if (!section_open || equal == std::string_view::npos) {
        add_issue(issues, line_number, current_id, "expected key=value inside a plugin section");
        ok = false;
      } else {
        const std::string key(trim(line.substr(0, equal)));
        const std::string_view value = trim(line.substr(equal + 1));
        if (key.empty() || !keys.insert(key).second) {
          add_issue(issues, line_number, current_id, "duplicate or empty key");
          ok = false;
        } else if (key == "path") {
          current.path = std::string(value);
          if (!valid_path(current.path)) {
            add_issue(issues, line_number, current_id, "path must be absolute and end in .sprx or .prx");
            ok = false;
          }
        } else if (key == "exact_title_ids") {
          if (!split_csv(value, &current.exact_title_ids, valid_title_id, 64)) {
            add_issue(issues, line_number, current_id, "invalid exact_title_ids");
            ok = false;
          }
        } else if (key == "title_id_prefixes") {
          if (!split_csv(value, &current.title_id_prefixes, valid_title_prefix, 64)) {
            add_issue(issues, line_number, current_id, "invalid title_id_prefixes");
            ok = false;
          }
        } else if (key == "auto_start") {
          if (!parse_bool(value, &current.auto_start)) {
            add_issue(issues, line_number, current_id, "auto_start must be true or false");
            ok = false;
          }
        } else if (key == "priority") {
          if (!parse_priority(value, &current.priority)) {
            add_issue(issues, line_number, current_id, "priority must be a signed 32-bit integer");
            ok = false;
          }
        } else if (key == "dependencies") {
          if (!split_csv(value, &current.dependencies, valid_id, kMaxDependencies)) {
            add_issue(issues, line_number, current_id, "invalid dependencies");
            ok = false;
          }
        } else {
          add_issue(issues, line_number, current_id, "unknown manifest key");
          ok = false;
        }
      }
    }
    if (newline == std::string_view::npos)
      break;
    cursor = newline + 1;
    ++line_number;
  }
  finish();

  if (!ok) {
    entries_.clear();
    return false;
  }
  entries_ = std::move(parsed);
  return true;
}

bool SprxCatalog::load_file(std::string_view path,
                            std::vector<SprxCatalogIssue> *issues) {
  if (issues)
    issues->clear();
  const std::string file_path(path);
  FILE *file = std::fopen(file_path.c_str(), "rb");
  if (!file) {
    add_issue(issues, 0, {}, std::string("cannot open manifest: ") + std::strerror(errno));
    entries_.clear();
    return false;
  }
  if (std::fseek(file, 0, SEEK_END) != 0) {
    std::fclose(file);
    add_issue(issues, 0, {}, "cannot seek manifest");
    entries_.clear();
    return false;
  }
  const long size = std::ftell(file);
  if (size < 0 || static_cast<unsigned long>(size) > kMaxManifestBytes) {
    std::fclose(file);
    add_issue(issues, 0, {}, "manifest exceeds size limit");
    entries_.clear();
    return false;
  }
  std::rewind(file);
  std::string text(static_cast<size_t>(size), '\0');
  const size_t read = std::fread(text.data(), 1, text.size(), file);
  const bool close_ok = std::fclose(file) == 0;
  if (read != text.size() || !close_ok) {
    add_issue(issues, 0, {}, "cannot read manifest");
    entries_.clear();
    return false;
  }
  return parse(text, issues);
}

const SprxManifestEntry *SprxCatalog::find(std::string_view id) const noexcept {
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&](const SprxManifestEntry &entry) {
                                    return entry.id == id;
                                  });
  return found == entries_.end() ? nullptr : &*found;
}

bool SprxCatalog::matches(const SprxManifestEntry &entry,
                          std::string_view title_id) const noexcept {
  if (!valid_title_id(title_id))
    return false;
  if (std::find(entry.exact_title_ids.begin(), entry.exact_title_ids.end(),
                title_id) != entry.exact_title_ids.end())
    return true;
  return std::any_of(entry.title_id_prefixes.begin(), entry.title_id_prefixes.end(),
                     [&](const std::string &prefix) {
                       return title_id.size() >= prefix.size() &&
                              title_id.compare(0, prefix.size(), prefix) == 0;
                     });
}

std::vector<const SprxManifestEntry *>
SprxCatalog::startup_order(std::string_view title_id,
                           std::vector<SprxCatalogIssue> *issues) const {
  if (issues)
    issues->clear();
  std::map<std::string, const SprxManifestEntry *> matched;
  for (const SprxManifestEntry &entry : entries_) {
    if (matches(entry, title_id))
      matched.emplace(entry.id, &entry);
  }

  std::set<std::string> required;
  std::set<std::string> visiting;
  bool ok = true;
  const auto collect = [&](const auto &self, const std::string &id) -> void {
    if (!ok)
      return;
    if (required.contains(id))
      return;
    if (!visiting.insert(id).second) {
      add_issue(issues, 0, id, "dependency cycle detected");
      ok = false;
      return;
    }
    const auto found = matched.find(id);
    if (found == matched.end()) {
      if (!find(id))
        add_issue(issues, 0, id, "dependency is not declared");
      else
        add_issue(issues, 0, id, "dependency does not match the target Title ID");
      ok = false;
      visiting.erase(id);
      return;
    }
    for (const std::string &dependency : found->second->dependencies)
      self(self, dependency);
    visiting.erase(id);
    required.insert(id);
  };

  for (const auto &[id, entry] : matched) {
    if (entry->auto_start)
      collect(collect, id);
  }
  if (!ok)
    return {};

  std::map<std::string, size_t> indegree;
  std::map<std::string, std::vector<std::string>> dependents;
  for (const std::string &id : required)
    indegree[id] = 0;
  for (const std::string &id : required) {
    const SprxManifestEntry *entry = matched.at(id);
    for (const std::string &dependency : entry->dependencies) {
      if (!required.contains(dependency))
        continue;
      ++indegree[id];
      dependents[dependency].push_back(id);
    }
  }

  std::vector<const SprxManifestEntry *> order;
  while (order.size() < required.size()) {
    const SprxManifestEntry *best = nullptr;
    for (const auto &[id, degree] : indegree) {
      if (degree != 0)
        continue;
      const auto *candidate = matched.at(id);
      if (!best || candidate->priority > best->priority ||
          (candidate->priority == best->priority && candidate->id < best->id))
        best = candidate;
    }
    if (!best) {
      add_issue(issues, 0, {}, "dependency cycle detected");
      return {};
    }
    order.push_back(best);
    indegree.erase(best->id);
    for (const std::string &dependent : dependents[best->id])
      --indegree[dependent];
  }
  return order;
}

} // namespace onion::sprx
