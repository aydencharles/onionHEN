#include "usb_payload_ui.hpp"

#include "onpress.hpp"
#include "settings_page_refresh.hpp"
#include "toolbox_i18n.hpp"
#include "usb_payload_action.hpp"
#include "usb_payload_importer.hpp"
#include "usb_storage.hpp"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <map>
#include <vector>

namespace {

using onion::shellui::usb_payloads::Entry;
using onion::shellui::usb_payloads::ImportResult;
std::vector<Entry> entries;
std::map<unsigned, onion::shellui::usb_storage::Device> devices_by_index;

std::string size_text(std::uint64_t bytes) {
  char buffer[48];
  if (bytes < 1024)
    std::snprintf(buffer, sizeof(buffer), "%llu B",
                  static_cast<unsigned long long>(bytes));
  else if (bytes < 1024 * 1024)
    std::snprintf(buffer, sizeof(buffer), "%.1f KB",
                  static_cast<double>(bytes) / 1024);
  else
    std::snprintf(buffer, sizeof(buffer), "%.1f MB",
                  static_cast<double>(bytes) / (1024 * 1024));
  return buffer;
}

bool model(ps5ui::Node &root) {
  using namespace onion::shellui;
  entries.clear();
  devices_by_index.clear();
  bool device_found = false;
  std::vector<usb_storage::Device> devices;
  if (usb_storage::list_devices(devices) >= 0) {
    for (const auto &device : devices) {
      usb_storage::with_device(device, [&](const usb_storage::Mapping &view) {
        if (devices_by_index.contains(view.usb_index))
          return;
        bool opened = false;
        auto found = usb_payloads::scan_directory(
            view.path, usb_payloads::kDestination, view.usb_index, &opened);
        if (!opened)
          return;
        device_found = true;
        devices_by_index.emplace(view.usb_index, device);
        entries.insert(entries.end(), std::make_move_iterator(found.begin()),
                        std::make_move_iterator(found.end()));
      });
    }
  }
  std::sort(entries.begin(), entries.end(), [](const Entry &left,
                                             const Entry &right) {
    return left.usb_index != right.usb_index
               ? left.usb_index < right.usb_index : left.name < right.name;
  });
  ps5ui::Page page("id_usb_payloads", toolbox_i18n::tr("usb_payloads.title"));
  if (!device_found)
    page.label("id_usb_payloads_empty",
               toolbox_i18n::tr("usb_payloads.no_device"));
  else if (entries.empty())
    page.label("id_usb_payloads_empty",
               toolbox_i18n::tr("usb_payloads.no_elf"));

  for (const Entry &entry : entries) {
    const std::string source = "usb" + std::to_string(entry.usb_index);
    const std::string detail =
        std::string(toolbox_i18n::tr(entry.installed ? "usb_payloads.replace"
                                                     : "usb_payloads.import")) +
        " · " + source + " · " + size_text(entry.size) + " · " +
        toolbox_i18n::tr(entry.installed ? "usb_payloads.installed"
                                         : "usb_payloads.not_installed");
    page.button(
        onion::shellui::usb_payloads::action_id(entry, entry.installed),
        entry.name, detail, std::nullopt, std::nullopt, ps5ui::Style::None,
        entry.installed ? std::optional<std::string>(
                              toolbox_i18n::tr("usb_payloads.replace_confirm"))
                        : std::nullopt,
        entry.installed ? std::optional<std::string>(
                              toolbox_i18n::tr("usb_payloads.confirm_phrase"))
                        : std::nullopt);
  }
  page.button("id_usb_payload_refresh",
              toolbox_i18n::tr("usb_payloads.refresh"));
  root = page.root();
  return true;
}

void message(const char *key, const std::string &name) {
  const std::string text = toolbox_i18n::format(key, name.c_str());
  notify("%s", text.c_str());
}

const char *error_key(ImportResult result) {
  switch (result) {
  case ImportResult::Completed:
    return "usb_payloads.completed";
  case ImportResult::InvalidName:
    return "usb_payloads.invalid_name";
  case ImportResult::CannotOpenSource:
    return "usb_payloads.cannot_open_source";
  case ImportResult::CannotCreateDestination:
    return "usb_payloads.cannot_create_destination";
  case ImportResult::DestinationAlreadyExists:
    return "usb_payloads.already_exists";
  case ImportResult::ReadError:
    return "usb_payloads.read_error";
  case ImportResult::WriteError:
    return "usb_payloads.write_error";
  case ImportResult::RenameError:
    return "usb_payloads.rename_error";
  }
  return "usb_payloads.failed";
}

} // namespace

void generate_usb_payload_xml(std::string &xml_buffer) {
  ps5ui::Node root;
  model(root);
  xml_buffer = onion::shellui::settings::publish(root, model);
}

OnPressResult onpress_usb_payload(OnPressContext &ctx) {
  ctx.dirty = false;
  if (ctx.id == "id_usb_payload_refresh") {
    onion::shellui::settings::refresh(ctx.instance);
    return OnPressResult::Consumed;
  }
  const std::string_view id = ctx.id;
  const bool replace = id.starts_with(
      onion::shellui::usb_payloads::kReplaceActionPrefix);
  if (!replace && !id.starts_with(
                      onion::shellui::usb_payloads::kImportActionPrefix))
    return OnPressResult::NotMine;
  const Entry *found = onion::shellui::usb_payloads::find_action_entry(
      entries, id, replace);
  if (!found)
    return OnPressResult::Consumed;
  const Entry entry = *found;
  ImportResult result = ImportResult::CannotOpenSource;
  const auto owner = devices_by_index.find(entry.usb_index);
  if (owner != devices_by_index.end()) {
    const auto device = owner->second;
    onion::shellui::usb_storage::with_device(
        device, [&](const onion::shellui::usb_storage::Mapping &view) {
          if (view.usb_index != entry.usb_index)
            return;
          Entry current = entry;
          current.source_path = view.path + "/" + entry.name;
          result = onion::shellui::usb_payloads::import_payload(
              current, onion::shellui::usb_payloads::kDestination, replace);
        });
  }
  message(result == ImportResult::Completed && replace ? "usb_payloads.replaced"
                                                       : error_key(result),
          entry.name);
  onion::shellui::settings::refresh(ctx.instance);
  return OnPressResult::Consumed;
}
