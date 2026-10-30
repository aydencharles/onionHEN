#include "usb_payload_ui.hpp"

#include "onpress.hpp"
#include "settings_page_refresh.hpp"
#include "toolbox_i18n.hpp"
#include "usb_payload_importer.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

using onion::shellui::usb_payloads::Entry;
using onion::shellui::usb_payloads::ImportResult;
std::vector<Entry> entries;

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
  bool device_found = false;
  entries = onion::shellui::usb_payloads::scan(
      onion::shellui::usb_payloads::kUsbPrefix,
      onion::shellui::usb_payloads::kDestination, &device_found);
  ps5ui::Page page("id_usb_payloads", toolbox_i18n::tr("usb_payloads.title"));
  if (!device_found)
    page.label("id_usb_payloads_empty",
               toolbox_i18n::tr("usb_payloads.no_device"));
  else if (entries.empty())
    page.label("id_usb_payloads_empty",
               toolbox_i18n::tr("usb_payloads.no_elf"));

  for (size_t i = 0; i < entries.size(); ++i) {
    const Entry &entry = entries[i];
    const std::string index = std::to_string(i);
    const std::string source = "usb" + std::to_string(entry.usb_index);
    const std::string detail =
        std::string(toolbox_i18n::tr(entry.installed ? "usb_payloads.replace"
                                                     : "usb_payloads.import")) +
        " · " + source + " · " + size_text(entry.size) + " · " +
        toolbox_i18n::tr(entry.installed ? "usb_payloads.installed"
                                         : "usb_payloads.not_installed");
    page.button(
        "id_usb_payload_" +
            std::string(entry.installed ? "replace_" : "import_") + index,
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
  constexpr const char *kImport = "id_usb_payload_import_";
  constexpr const char *kReplace = "id_usb_payload_replace_";
  const bool replace = ctx.id.rfind(kReplace, 0) == 0;
  const char *prefix = replace ? kReplace : kImport;
  if (ctx.id.rfind(prefix, 0) != 0)
    return OnPressResult::NotMine;
  const std::string number =
      ctx.id.substr(std::char_traits<char>::length(prefix));
  if (number.empty() || !std::all_of(number.begin(), number.end(), [](char c) {
        return c >= '0' && c <= '9';
      }))
    return OnPressResult::Consumed;
  const size_t index =
      static_cast<size_t>(std::strtoul(number.c_str(), nullptr, 10));
  if (index >= entries.size())
    return OnPressResult::Consumed;
  const Entry entry = entries[index];
  const ImportResult result = onion::shellui::usb_payloads::import_payload(
      entry, onion::shellui::usb_payloads::kDestination, replace);
  message(result == ImportResult::Completed && replace ? "usb_payloads.replaced"
                                                       : error_key(result),
          entry.name);
  onion::shellui::settings::refresh(ctx.instance);
  return OnPressResult::Consumed;
}
