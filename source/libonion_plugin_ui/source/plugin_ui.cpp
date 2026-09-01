#include "onion/plugin_ui.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>

namespace onion::plugin_ui {
namespace {

struct Reader {
  std::span<const uint8_t> data;
  size_t offset = 0;

  bool take(size_t size, std::span<const uint8_t> &out) {
    if (size > data.size() - offset) return false;
    out = data.subspan(offset, size);
    offset += size;
    return true;
  }

  bool u16(uint16_t &out) {
    std::span<const uint8_t> bytes;
    if (!take(2, bytes)) return false;
    out = static_cast<uint16_t>(bytes[0]) |
          static_cast<uint16_t>(bytes[1] << 8);
    return true;
  }

  bool u32(uint32_t &out) {
    std::span<const uint8_t> bytes;
    if (!take(4, bytes)) return false;
    out = static_cast<uint32_t>(bytes[0]) |
          (static_cast<uint32_t>(bytes[1]) << 8) |
          (static_cast<uint32_t>(bytes[2]) << 16) |
          (static_cast<uint32_t>(bytes[3]) << 24);
    return true;
  }

  bool u64(uint64_t &out) {
    std::span<const uint8_t> bytes;
    if (!take(8, bytes)) return false;
    out = 0;
    for (unsigned i = 0; i < 8; ++i)
      out |= static_cast<uint64_t>(bytes[i]) << (i * 8);
    return true;
  }

  bool text(size_t size, std::string &out) {
    std::span<const uint8_t> bytes;
    if (!take(size, bytes)) return false;
    if (std::find(bytes.begin(), bytes.end(), uint8_t{0}) != bytes.end())
      return false;
    out.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    return true;
  }
};

void fail(std::string *error, std::string_view message) {
  if (error) *error = message;
}

bool valid_id(std::string_view id, size_t max_size) {
  if (id.empty() || id.size() >= max_size) return false;
  return std::all_of(id.begin(), id.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
  });
}

bool kind_valid(NodeKind kind) {
  const auto value = static_cast<uint16_t>(kind);
  return value >= static_cast<uint16_t>(NodeKind::Page) &&
         value <= static_cast<uint16_t>(NodeKind::Input);
}

bool value_type_valid(ValueType type) {
  return static_cast<uint32_t>(type) <= static_cast<uint32_t>(ValueType::String);
}

bool binding_valid(BindingKind binding) {
  return static_cast<uint32_t>(binding) <=
         static_cast<uint32_t>(BindingKind::Event);
}

bool parent_accepts(NodeKind parent, NodeKind child) {
  if (child == NodeKind::ListItem) return parent == NodeKind::List;
  if (child == NodeKind::Page) return false;
  return parent == NodeKind::Page || parent == NodeKind::Group;
}

bool parse_integer(std::string_view text, int64_t &out) {
  if (text.empty() || text.size() >= 64) return false;
  char buffer[64];
  std::copy(text.begin(), text.end(), buffer);
  buffer[text.size()] = '\0';
  char *end = nullptr;
  errno = 0;
  const long long value = std::strtoll(buffer, &end, 10);
  if (errno != 0 || end == buffer || *end != '\0') return false;
  out = static_cast<int64_t>(value);
  return true;
}

bool value_valid(const Node &node, ValueType value_type, std::string_view value) {
  if (value_type != node.value_type || value.size() >= 256) return false;
  if (value_type == ValueType::Bool)
    return value == "0" || value == "1" || value == "true" || value == "false";
  if (value_type == ValueType::Integer) {
    int64_t parsed = 0;
    if (!parse_integer(value, parsed)) return false;
    return node.min_value > node.max_value ||
           (parsed >= node.min_value && parsed <= node.max_value);
  }
  return value_type == ValueType::String &&
         value.size() >= node.min_length &&
         (node.max_length == 0 || value.size() <= node.max_length);
}

uint16_t read_u16(std::span<const uint8_t> bytes, size_t offset) {
  return static_cast<uint16_t>(bytes[offset]) |
         (static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

uint32_t read_u32(std::span<const uint8_t> bytes, size_t offset) {
  return static_cast<uint32_t>(bytes[offset]) |
         (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

uint64_t read_u64(std::span<const uint8_t> bytes, size_t offset) {
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i)
    value |= static_cast<uint64_t>(bytes[offset + i]) << (i * 8);
  return value;
}

void append_u32(std::vector<uint8_t> &bytes, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    bytes.push_back(static_cast<uint8_t>(value >> (i * 8)));
}

void append_u64(std::vector<uint8_t> &bytes, uint64_t value) {
  for (unsigned i = 0; i < 8; ++i)
    bytes.push_back(static_cast<uint8_t>(value >> (i * 8)));
}

} // namespace

const Node *Document::find_node(std::string_view id) const {
  auto it = std::find_if(nodes.begin(), nodes.end(),
                         [id](const Node &node) { return node.id == id; });
  return it == nodes.end() ? nullptr : &*it;
}

Node *Document::find_node(std::string_view id) {
  auto it = std::find_if(nodes.begin(), nodes.end(),
                         [id](const Node &node) { return node.id == id; });
  return it == nodes.end() ? nullptr : &*it;
}

Status decode_document(std::span<const uint8_t> encoded, Document &out,
                       std::string *error) {
  out = {};
  if (encoded.size() < kDocumentHeaderSize || encoded.size() > kMaxEncodedSize) {
    fail(error, "encoded document size is outside the supported range");
    return Status::InvalidEncoding;
  }
  Reader reader{encoded};
  uint32_t magic = 0, total_size = 0, node_count = 0, priority = 0;
  uint16_t version = 0, header_size = 0, reserved = 0;
  uint16_t lengths[5]{};
  if (!reader.u32(magic) || !reader.u16(version) || !reader.u16(header_size) ||
      !reader.u32(total_size) || !reader.u32(node_count) ||
      !reader.u32(out.flags) || !reader.u32(priority)) {
    fail(error, "truncated document header");
    return Status::InvalidEncoding;
  }
  for (uint16_t &length : lengths)
    if (!reader.u16(length)) return Status::InvalidEncoding;
  if (!reader.u16(reserved) || magic != kDocumentMagic ||
      version != kWireVersion || header_size != kDocumentHeaderSize ||
      total_size != encoded.size() || node_count == 0 || node_count > kMaxNodes ||
      reserved != 0) {
    fail(error, "invalid document header");
    return Status::InvalidEncoding;
  }
  out.priority = static_cast<int32_t>(priority);
  std::string *metadata[] = {&out.plugin_id, &out.contribution_id, &out.title,
                             &out.description, &out.root_page_id};
  for (size_t i = 0; i < 5; ++i) {
    if (!reader.text(lengths[i], *metadata[i])) {
      fail(error, "invalid document metadata");
      return Status::InvalidEncoding;
    }
  }

  out.nodes.reserve(node_count);
  for (uint32_t index = 0; index < node_count; ++index) {
    const size_t record_begin = reader.offset;
    uint32_t record_size = 0, flags = 0, value_type = 0, binding = 0;
    uint16_t kind = 0, node_reserved = 0, tail_reserved = 0;
    uint64_t min_value = 0, max_value = 0;
    uint16_t text_lengths[7]{};
    Node node;
    if (!reader.u32(record_size) || !reader.u16(kind) ||
        !reader.u16(node_reserved) || !reader.u32(flags) ||
        !reader.u32(value_type) || !reader.u32(binding) ||
        !reader.u64(min_value) || !reader.u64(max_value) ||
        !reader.u32(node.min_length) || !reader.u32(node.max_length)) {
      fail(error, "truncated node header");
      return Status::InvalidEncoding;
    }
    for (uint16_t &length : text_lengths)
      if (!reader.u16(length)) return Status::InvalidEncoding;
    if (!reader.u16(tail_reserved) || record_size < kNodeHeaderSize ||
        record_size > encoded.size() - record_begin || node_reserved != 0 ||
        tail_reserved != 0) {
      fail(error, "invalid node record header");
      return Status::InvalidEncoding;
    }
    node.kind = static_cast<NodeKind>(kind);
    node.flags = flags;
    node.value_type = static_cast<ValueType>(value_type);
    node.binding = static_cast<BindingKind>(binding);
    node.min_value = static_cast<int64_t>(min_value);
    node.max_value = static_cast<int64_t>(max_value);
    std::string *texts[] = {&node.id, &node.parent_id, &node.title,
                            &node.description, &node.target_id,
                            &node.binding_key, &node.value};
    for (size_t i = 0; i < 7; ++i) {
      if (!reader.text(text_lengths[i], *texts[i])) {
        fail(error, "invalid node string");
        return Status::InvalidEncoding;
      }
    }
    if (reader.offset - record_begin != record_size) {
      fail(error, "node record size does not match its payload");
      return Status::InvalidEncoding;
    }
    out.nodes.push_back(std::move(node));
  }
  if (reader.offset != encoded.size()) {
    fail(error, "trailing bytes after UI document");
    return Status::InvalidEncoding;
  }
  return validate_document(out, error);
}

Status validate_document(const Document &document, std::string *error) {
  if (!valid_id(document.plugin_id, 32) ||
      !valid_id(document.contribution_id, 64) ||
      !valid_id(document.root_page_id, 64) || document.title.empty() ||
      document.title.size() >= 128 || document.description.size() >= 256 ||
      document.nodes.empty() || document.nodes.size() > kMaxNodes) {
    fail(error, "invalid contribution metadata");
    return Status::InvalidDocument;
  }
  std::unordered_map<std::string_view, const Node *> by_id;
  for (const Node &node : document.nodes) {
    if (!kind_valid(node.kind) || !value_type_valid(node.value_type) ||
        !binding_valid(node.binding) || !valid_id(node.id, 64) ||
        node.title.empty() || node.title.size() >= 128 ||
        node.description.size() >= 256 || node.value.size() >= 256 ||
        !by_id.emplace(node.id, &node).second) {
      fail(error, "invalid or duplicate UI node");
      return Status::InvalidDocument;
    }
  }
  const Node *root = document.find_node(document.root_page_id);
  if (!root || root->kind != NodeKind::Page) {
    fail(error, "root page is missing");
    return Status::InvalidDocument;
  }
  for (const Node &node : document.nodes) {
    if (node.kind == NodeKind::Page) {
      if (!node.parent_id.empty()) return Status::InvalidDocument;
    } else {
      const Node *parent = document.find_node(node.parent_id);
      if (!parent || !parent_accepts(parent->kind, node.kind)) {
        fail(error, "node has an invalid parent");
        return Status::InvalidDocument;
      }
    }
    if (node.kind == NodeKind::Menu) {
      const Node *target = document.find_node(node.target_id);
      if (!target || target->kind != NodeKind::Page) {
        fail(error, "menu target is not a page");
        return Status::InvalidDocument;
      }
    } else if (!node.target_id.empty()) {
      return Status::InvalidDocument;
    }
    if (node.kind == NodeKind::Action) {
      if (node.binding != BindingKind::Event || node.binding_key.empty() ||
          node.value_type != ValueType::None) return Status::InvalidDocument;
    } else if (node.binding != BindingKind::None && node.binding_key.empty()) {
      return Status::InvalidDocument;
    }
    if (node.kind == NodeKind::Toggle && node.value_type != ValueType::Bool)
      return Status::InvalidDocument;
    if ((node.kind == NodeKind::List || node.kind == NodeKind::Input ||
         node.kind == NodeKind::ListItem) &&
        node.value_type != ValueType::Integer &&
        node.value_type != ValueType::String) return Status::InvalidDocument;
    if ((node.kind == NodeKind::Page || node.kind == NodeKind::Menu ||
         node.kind == NodeKind::Group || node.kind == NodeKind::Label) &&
        (node.value_type != ValueType::None ||
         node.binding != BindingKind::None)) return Status::InvalidDocument;
    if (node.max_length != 0 && node.min_length > node.max_length)
      return Status::InvalidDocument;
    if ((node.kind == NodeKind::Toggle || node.kind == NodeKind::List ||
         node.kind == NodeKind::Input || node.kind == NodeKind::ListItem) &&
        !value_valid(node, node.value_type, node.value)) {
      fail(error, "node has an invalid initial value");
      return Status::InvalidDocument;
    }
    if (node.kind == NodeKind::List) {
      const bool has_item = std::any_of(
          document.nodes.begin(), document.nodes.end(), [&node](const Node &child) {
            return child.kind == NodeKind::ListItem && child.parent_id == node.id;
          });
      const bool has_selected_item = std::any_of(
          document.nodes.begin(), document.nodes.end(), [&node](const Node &child) {
            return child.kind == NodeKind::ListItem &&
                   child.parent_id == node.id &&
                   child.value_type == node.value_type &&
                   child.value == node.value;
          });
      if (!has_item || !has_selected_item) return Status::InvalidDocument;
    }
    if (node.kind == NodeKind::ListItem) {
      const Node *parent = document.find_node(node.parent_id);
      if (!parent || parent->kind != NodeKind::List ||
          parent->value_type != node.value_type)
        return Status::InvalidDocument;
    }
    std::unordered_set<std::string_view> ancestors;
    const Node *cursor = &node;
    size_t depth = 0;
    while (!cursor->parent_id.empty()) {
      if (!ancestors.insert(cursor->id).second || ++depth > kMaxDepth)
        return Status::InvalidDocument;
      cursor = document.find_node(cursor->parent_id);
      if (!cursor) return Status::InvalidDocument;
    }
  }
  return Status::Ok;
}

const ContributionSnapshot *RegistrySnapshot::find(Handle handle) const {
  auto it = std::find_if(contributions.begin(), contributions.end(),
                         [handle](const ContributionSnapshot &entry) {
                           return entry.handle == handle;
                         });
  return it == contributions.end() ? nullptr : &*it;
}

RegisterResult Registry::register_encoded(std::string_view owner,
                                          std::span<const uint8_t> encoded) {
  RegisterResult result;
  if (!valid_id(owner, 32)) {
    result.status = Status::InvalidArgument;
    result.error = "invalid contribution owner";
    return result;
  }
  auto document = std::make_shared<Document>();
  result.status = decode_document(encoded, *document, &result.error);
  if (result.status != Status::Ok) return result;
  if (document->plugin_id != owner) {
    result.status = Status::PermissionDenied;
    result.error = "document plugin_id does not match authenticated owner";
    return result;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = std::find_if(contributions_.begin(), contributions_.end(),
                         [&](const ContributionSnapshot &entry) {
                           return entry.owner == owner && entry.document &&
                                  entry.document->contribution_id ==
                                      document->contribution_id;
                         });
  if (it == contributions_.end()) {
    result.handle = next_handle_++;
    contributions_.push_back({result.handle, std::string(owner), document});
  } else {
    result.handle = it->handle;
    it->document = std::move(document);
  }
  result.generation = ++generation_;
  result.status = Status::Ok;
  return result;
}

Status Registry::unregister(std::string_view owner, Handle handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = std::find_if(contributions_.begin(), contributions_.end(),
                         [&](const ContributionSnapshot &entry) {
                           return entry.handle == handle && entry.owner == owner;
                         });
  if (it == contributions_.end()) return Status::NotFound;
  contributions_.erase(it);
  ++generation_;
  return Status::Ok;
}

size_t Registry::remove_owner(std::string_view owner) {
  std::lock_guard<std::mutex> lock(mutex_);
  const size_t before = contributions_.size();
  std::erase_if(contributions_,
                [&](const ContributionSnapshot &entry) { return entry.owner == owner; });
  const size_t removed = before - contributions_.size();
  if (removed != 0) ++generation_;
  return removed;
}

Status Registry::set_value(std::string_view owner, Handle handle,
                           std::string_view node_id, ValueType value_type,
                           std::string_view value) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = std::find_if(contributions_.begin(), contributions_.end(),
                         [&](const ContributionSnapshot &entry) {
                           return entry.handle == handle && entry.owner == owner;
                         });
  if (it == contributions_.end() || !it->document) return Status::NotFound;
  auto updated = std::make_shared<Document>(*it->document);
  Node *node = updated->find_node(node_id);
  if (!node) return Status::NotFound;
  if (!value_valid(*node, value_type, value)) return Status::InvalidArgument;
  if (node->kind == NodeKind::List) {
    const bool known = std::any_of(
        updated->nodes.begin(), updated->nodes.end(), [&](const Node &item) {
          return item.kind == NodeKind::ListItem && item.parent_id == node->id &&
                 item.value_type == value_type && item.value == value;
        });
    if (!known) return Status::InvalidArgument;
  }
  node->value.assign(value);
  it->document = std::move(updated);
  ++generation_;
  return Status::Ok;
}

RegistrySnapshot Registry::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return {generation_, contributions_};
}

BeginResult RegistrationBroker::begin(std::string_view owner,
                                      uint32_t total_size,
                                      uint32_t checksum) {
  if (!valid_id(owner, 32) || total_size < kDocumentHeaderSize ||
      total_size > kMaxEncodedSize) {
    return {};
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const size_t owner_transfers = static_cast<size_t>(std::count_if(
      transfers_.begin(), transfers_.end(),
      [owner](const Transfer &transfer) { return transfer.owner == owner; }));
  if (owner_transfers >= 4 || transfers_.size() >= 32)
    return {Status::Stale, 0};
  TransferId id = next_transfer_id_++;
  if (id == 0) id = next_transfer_id_++;
  Transfer transfer;
  transfer.id = id;
  transfer.owner = owner;
  transfer.checksum = checksum;
  transfer.bytes.resize(total_size);
  transfers_.push_back(std::move(transfer));
  return {Status::Ok, id};
}

Status RegistrationBroker::append(std::string_view owner,
                                  TransferId transfer_id, uint32_t offset,
                                  std::span<const uint8_t> chunk) {
  if (chunk.empty()) return Status::InvalidArgument;
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = std::find_if(transfers_.begin(), transfers_.end(),
                         [&](const Transfer &transfer) {
                           return transfer.id == transfer_id &&
                                  transfer.owner == owner;
                         });
  if (it == transfers_.end()) return Status::NotFound;
  if (offset != it->received || chunk.size() > it->bytes.size() - it->received)
    return Status::Stale;
  std::copy(chunk.begin(), chunk.end(), it->bytes.begin() + it->received);
  it->received += chunk.size();
  return Status::Ok;
}

RegisterResult RegistrationBroker::commit(std::string_view owner,
                                          TransferId transfer_id) {
  std::vector<uint8_t> bytes;
  uint32_t expected_checksum = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(transfers_.begin(), transfers_.end(),
                           [&](const Transfer &transfer) {
                             return transfer.id == transfer_id &&
                                    transfer.owner == owner;
                           });
    if (it == transfers_.end())
      return {Status::NotFound, 0, 0, "registration transfer was not found"};
    if (it->received != it->bytes.size())
      return {Status::Stale, 0, 0, "registration transfer is incomplete"};
    bytes = std::move(it->bytes);
    expected_checksum = it->checksum;
    transfers_.erase(it);
  }
  uint32_t checksum = 2166136261u;
  for (uint8_t byte : bytes) {
    checksum ^= byte;
    checksum *= 16777619u;
  }
  if (checksum != expected_checksum)
    return {Status::InvalidEncoding, 0, 0,
            "registration transfer checksum mismatch"};
  return registry_.register_encoded(owner, bytes);
}

Status RegistrationBroker::abort(std::string_view owner,
                                 TransferId transfer_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = std::find_if(transfers_.begin(), transfers_.end(),
                         [&](const Transfer &transfer) {
                           return transfer.id == transfer_id &&
                                  transfer.owner == owner;
                         });
  if (it == transfers_.end()) return Status::NotFound;
  transfers_.erase(it);
  return Status::Ok;
}

size_t RegistrationBroker::disconnect(std::string_view owner) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    std::erase_if(transfers_,
                  [owner](const Transfer &transfer) { return transfer.owner == owner; });
  }
  return registry_.remove_owner(owner);
}

ProtocolBroker::ProtocolBroker(Registry &registry)
    : registry_(registry), registrations_(registry) {}

void ProtocolBroker::set_snapshot_sink(SnapshotSink sink, void *context) {
  std::lock_guard<std::mutex> lock(sink_mutex_);
  snapshot_sink_ = sink;
  snapshot_context_ = context;
}

void ProtocolBroker::publish_snapshot() {
  SnapshotSink sink = nullptr;
  void *context = nullptr;
  {
    std::lock_guard<std::mutex> lock(sink_mutex_);
    sink = snapshot_sink_;
    context = snapshot_context_;
  }
  if (sink) sink(registry_.snapshot(), context);
}

WireResponse ProtocolBroker::dispatch(std::string_view owner, uint16_t command,
                                      std::span<const uint8_t> payload) {
  if (!valid_id(owner, 32)) return {};

  switch (static_cast<WireCommand>(command)) {
  case WireCommand::RegisterBegin: {
    if (payload.size() != 8) return {};
    const BeginResult result =
        registrations_.begin(owner, read_u32(payload, 0), read_u32(payload, 4));
    WireResponse response{result.status, {}};
    if (result.status == Status::Ok)
      append_u32(response.data, result.transfer_id);
    return response;
  }
  case WireCommand::RegisterChunk: {
    if (payload.size() <= 8) return {};
    return {registrations_.append(owner, read_u32(payload, 0),
                                  read_u32(payload, 4), payload.subspan(8)),
            {}};
  }
  case WireCommand::RegisterCommit: {
    if (payload.size() != 4) return {};
    const RegisterResult result =
        registrations_.commit(owner, read_u32(payload, 0));
    WireResponse response{result.status, {}};
    if (result.status == Status::Ok) {
      append_u64(response.data, result.handle);
      publish_snapshot();
    }
    return response;
  }
  case WireCommand::RegisterAbort:
    if (payload.size() != 4) return {};
    return {registrations_.abort(owner, read_u32(payload, 0)), {}};
  case WireCommand::Unregister: {
    if (payload.size() != 8) return {};
    const Status status = registry_.unregister(owner, read_u64(payload, 0));
    if (status == Status::Ok) publish_snapshot();
    return {status, {}};
  }
  case WireCommand::SetValue: {
    if (payload.size() < 16) return {};
    const uint16_t node_id_size = read_u16(payload, 12);
    const uint16_t value_size = read_u16(payload, 14);
    if (node_id_size == 0 ||
        static_cast<size_t>(node_id_size) + value_size != payload.size() - 16)
      return {};
    const auto as_text = [](std::span<const uint8_t> bytes) {
      return std::string_view(reinterpret_cast<const char *>(bytes.data()),
                              bytes.size());
    };
    const std::string_view node_id =
        as_text(payload.subspan(16, node_id_size));
    const std::string_view value =
        as_text(payload.subspan(16 + node_id_size, value_size));
    if (node_id.find('\0') != std::string_view::npos ||
        value.find('\0') != std::string_view::npos)
      return {};
    const Status status = registry_.set_value(
        owner, read_u64(payload, 0), node_id,
        static_cast<ValueType>(read_u32(payload, 8)), value);
    if (status == Status::Ok) publish_snapshot();
    return {status, {}};
  }
  }
  return {};
}

size_t ProtocolBroker::disconnect(std::string_view owner) {
  const size_t removed = registrations_.disconnect(owner);
  if (removed != 0) publish_snapshot();
  return removed;
}

} // namespace onion::plugin_ui
