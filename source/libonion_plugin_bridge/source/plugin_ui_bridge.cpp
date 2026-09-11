#include <onion/plugin_ui_bridge.hpp>

#include <algorithm>
#include <limits>
#include <memory>

namespace onion::plugin_bridge {
namespace {

struct Reader {
  std::span<const uint8_t> bytes;
  size_t offset = 0;

  bool take(size_t count, std::span<const uint8_t> &out) {
    if (offset > bytes.size() || count > bytes.size() - offset) return false;
    out = bytes.subspan(offset, count);
    offset += count;
    return true;
  }
  bool u16(uint16_t &out) {
    std::span<const uint8_t> value;
    if (!take(2, value)) return false;
    out = static_cast<uint16_t>(value[0]) |
          (static_cast<uint16_t>(value[1]) << 8);
    return true;
  }
  bool u32(uint32_t &out) {
    std::span<const uint8_t> value;
    if (!take(4, value)) return false;
    out = static_cast<uint32_t>(value[0]) |
          (static_cast<uint32_t>(value[1]) << 8) |
          (static_cast<uint32_t>(value[2]) << 16) |
          (static_cast<uint32_t>(value[3]) << 24);
    return true;
  }
  bool u64(uint64_t &out) {
    std::span<const uint8_t> value;
    if (!take(8, value)) return false;
    out = 0;
    for (unsigned i = 0; i < 8; ++i)
      out |= static_cast<uint64_t>(value[i]) << (i * 8);
    return true;
  }
  bool text(size_t count, std::string &out) {
    std::span<const uint8_t> value;
    if (!take(count, value) ||
        std::find(value.begin(), value.end(), uint8_t{0}) != value.end())
      return false;
    out.assign(reinterpret_cast<const char *>(value.data()), value.size());
    return true;
  }
};

void u16(std::vector<uint8_t> &out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value));
  out.push_back(static_cast<uint8_t>(value >> 8));
}
void u32(std::vector<uint8_t> &out, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    out.push_back(static_cast<uint8_t>(value >> (i * 8)));
}
void u64(std::vector<uint8_t> &out, uint64_t value) {
  for (unsigned i = 0; i < 8; ++i)
    out.push_back(static_cast<uint8_t>(value >> (i * 8)));
}
void text(std::vector<uint8_t> &out, std::string_view value) {
  out.insert(out.end(), value.begin(), value.end());
}
void fail(std::string *error, std::string_view message) {
  if (error) error->assign(message);
}

} // namespace

bool encode_frame_header(const FrameHeader &header,
                         std::span<uint8_t, kFrameHeaderSize> out) {
  if (header.payload_size > kMaxPayloadSize ||
      (header.type != MessageType::Snapshot &&
       header.type != MessageType::Action))
    return false;
  std::fill(out.begin(), out.end(), uint8_t{0});
  for (unsigned i = 0; i < 4; ++i)
    out[i] = static_cast<uint8_t>(kMagic >> (i * 8));
  out[4] = static_cast<uint8_t>(kVersion);
  out[5] = static_cast<uint8_t>(kVersion >> 8);
  const uint16_t type = static_cast<uint16_t>(header.type);
  out[6] = static_cast<uint8_t>(type);
  out[7] = static_cast<uint8_t>(type >> 8);
  for (unsigned i = 0; i < 4; ++i)
    out[8 + i] = static_cast<uint8_t>(header.payload_size >> (i * 8));
  return true;
}

bool decode_frame_header(std::span<const uint8_t, kFrameHeaderSize> encoded,
                         FrameHeader &out) {
  Reader reader{encoded};
  uint32_t magic = 0, reserved = 0;
  uint16_t version = 0, type = 0;
  if (!reader.u32(magic) || !reader.u16(version) || !reader.u16(type) ||
      !reader.u32(out.payload_size) || !reader.u32(reserved) ||
      magic != kMagic || version != kVersion || reserved != 0 ||
      out.payload_size > kMaxPayloadSize)
    return false;
  out.type = static_cast<MessageType>(type);
  return out.type == MessageType::Snapshot || out.type == MessageType::Action;
}

bool encode_snapshot(const plugin_ui::RegistrySnapshot &snapshot,
                     std::vector<uint8_t> &out, std::string *error) {
  out.clear();
  if (snapshot.contributions.size() > kMaxContributions) {
    fail(error, "snapshot has too many contributions");
    return false;
  }
  u64(out, snapshot.generation);
  u32(out, static_cast<uint32_t>(snapshot.contributions.size()));
  u32(out, 0);
  for (const plugin_ui::ContributionSnapshot &entry : snapshot.contributions) {
    if (entry.handle == 0 || entry.owner.empty() || entry.owner.size() > UINT16_MAX ||
        !entry.document) {
      fail(error, "snapshot contains an invalid contribution");
      out.clear();
      return false;
    }
    std::vector<uint8_t> document;
    if (plugin_ui::encode_document(*entry.document, document, error) !=
        plugin_ui::Status::Ok) {
      out.clear();
      return false;
    }
    if (out.size() > kMaxPayloadSize ||
        16u + entry.owner.size() + document.size() >
            kMaxPayloadSize - out.size()) {
      fail(error, "snapshot exceeds the bridge payload limit");
      out.clear();
      return false;
    }
    u64(out, entry.handle);
    u16(out, static_cast<uint16_t>(entry.owner.size()));
    u16(out, 0);
    u32(out, static_cast<uint32_t>(document.size()));
    text(out, entry.owner);
    out.insert(out.end(), document.begin(), document.end());
  }
  return true;
}

bool decode_snapshot(std::span<const uint8_t> encoded,
                     plugin_ui::RegistrySnapshot &out, std::string *error) {
  out = {};
  if (encoded.size() < 16 || encoded.size() > kMaxPayloadSize) return false;
  Reader reader{encoded};
  uint32_t count = 0, reserved = 0;
  if (!reader.u64(out.generation) || !reader.u32(count) ||
      !reader.u32(reserved) || reserved != 0 || count > kMaxContributions)
    return false;
  out.contributions.reserve(count);
  for (uint32_t index = 0; index < count; ++index) {
    plugin_ui::ContributionSnapshot entry;
    uint16_t owner_size = 0, entry_reserved = 0;
    uint32_t document_size = 0;
    if (!reader.u64(entry.handle) || !reader.u16(owner_size) ||
        !reader.u16(entry_reserved) || !reader.u32(document_size) ||
        entry.handle == 0 || owner_size == 0 || entry_reserved != 0 ||
        document_size < plugin_ui::kDocumentHeaderSize ||
        document_size > plugin_ui::kMaxEncodedSize ||
        !reader.text(owner_size, entry.owner))
      return false;
    std::span<const uint8_t> document_bytes;
    if (!reader.take(document_size, document_bytes)) return false;
    auto document = std::make_shared<plugin_ui::Document>();
    if (plugin_ui::decode_document(document_bytes, *document, error) !=
            plugin_ui::Status::Ok ||
        document->plugin_id != entry.owner)
      return false;
    entry.document = std::move(document);
    out.contributions.push_back(std::move(entry));
  }
  return reader.offset == encoded.size();
}

bool encode_action(const ActionRequest &action, std::vector<uint8_t> &out) {
  out.clear();
  if (action.handle == 0 || action.node_id.empty() ||
      action.node_id.size() >= 64 || action.value.size() >= 256)
    return false;
  u64(out, action.handle);
  u16(out, static_cast<uint16_t>(action.node_id.size()));
  u16(out, static_cast<uint16_t>(action.value.size()));
  u32(out, 0);
  text(out, action.node_id);
  text(out, action.value);
  return true;
}

bool decode_action(std::span<const uint8_t> encoded, ActionRequest &out) {
  out = {};
  Reader reader{encoded};
  uint16_t node_size = 0, value_size = 0;
  uint32_t reserved = 0;
  if (!reader.u64(out.handle) || !reader.u16(node_size) ||
      !reader.u16(value_size) || !reader.u32(reserved) || out.handle == 0 ||
      node_size == 0 || node_size >= 64 || value_size >= 256 || reserved != 0 ||
      !reader.text(node_size, out.node_id) ||
      !reader.text(value_size, out.value))
    return false;
  return reader.offset == encoded.size();
}

bool encode_ui_event(const plugin_ui::ActionEvent &event,
                     std::vector<uint8_t> &out) {
  out.clear();
  const std::string *texts[] = {&event.contribution_id, &event.page_id,
                                &event.node_id, &event.value};
  if (event.owner.empty() || event.handle == 0 || event.sequence == 0 ||
      event.contribution_id.empty() || event.page_id.empty() ||
      event.node_id.empty())
    return false;
  for (const std::string *value : texts)
    if (value->size() > UINT16_MAX) return false;
  u32(out, kUiActionEventId);
  u32(out, kUiEventAbiVersion);
  u64(out, event.sequence);
  u64(out, event.handle);
  u32(out, static_cast<uint32_t>(event.value_type));
  for (const std::string *value : texts)
    u16(out, static_cast<uint16_t>(value->size()));
  for (const std::string *value : texts) text(out, *value);
  return true;
}

} // namespace onion::plugin_bridge
