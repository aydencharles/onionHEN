#include "test_harness.h"

#include "dynamic_ui_xml.hpp"
#include "dynamic_ui_runtime.hpp"
#include "ps5_settings_ui.hpp"
#include "plugin_ipc_server.hpp"
#include <onion/ipc_server.hpp>
#include <onion/plugin_session.hpp>
#include <onion/plugin_ui.hpp>
#include <onion/plugin_ui_bridge.hpp>

#include <cstdint>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using onion::plugin_ui::BindingKind;
using onion::plugin_ui::NodeKind;
using onion::plugin_ui::ValueType;

namespace {

bool g_action_received = false;
uint64_t g_published_generation = 0;

class TestEventSource final : public onion::plugin_session::EventSource {
public:
  onion::plugin_ui::WireResponse poll(std::string_view owner) override {
    polled_owner.assign(owner);
    if (event.empty())
      return {onion::plugin_ui::Status::NotFound, {}};
    std::vector<uint8_t> result = std::move(event);
    return {onion::plugin_ui::Status::Ok, std::move(result)};
  }

  void disconnect(std::string_view owner) override {
    disconnected_owner.assign(owner);
    event.clear();
  }

  std::vector<uint8_t> event;
  std::string polled_owner;
  std::string disconnected_owner;
};

bool action_sink(onion::plugin_ui::Handle, const onion::plugin_ui::Node &node,
                 std::string_view, void *) {
  g_action_received = node.id == "reload";
  return g_action_received;
}

void snapshot_sink(const onion::plugin_ui::RegistrySnapshot &snapshot, void *) {
  g_published_generation = snapshot.generation;
}

uint32_t read_u32(const std::vector<uint8_t> &bytes, size_t offset = 0) {
  return static_cast<uint32_t>(bytes[offset]) |
         (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

uint32_t read_u32(const uint8_t *bytes) {
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) |
         (static_cast<uint32_t>(bytes[3]) << 24);
}

uint64_t read_u64(const std::vector<uint8_t> &bytes, size_t offset = 0) {
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i)
    value |= static_cast<uint64_t>(bytes[offset + i]) << (i * 8);
  return value;
}

void put_u16(std::vector<uint8_t> &out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value));
  out.push_back(static_cast<uint8_t>(value >> 8));
}

void put_u32(std::vector<uint8_t> &out, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    out.push_back(static_cast<uint8_t>(value >> (i * 8)));
}

void put_u64(std::vector<uint8_t> &out, uint64_t value) {
  for (unsigned i = 0; i < 8; ++i)
    out.push_back(static_cast<uint8_t>(value >> (i * 8)));
}

void patch_u32(std::vector<uint8_t> &out, size_t offset, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    out[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}

struct EncodedNode {
  NodeKind kind;
  uint32_t flags = 0;
  ValueType value_type = ValueType::None;
  BindingKind binding = BindingKind::None;
  int64_t min_value = 0;
  int64_t max_value = 0;
  uint32_t min_length = 0;
  uint32_t max_length = 0;
  std::string id;
  std::string parent;
  std::string title;
  std::string description;
  std::string target;
  std::string binding_key;
  std::string value;
};

void append_node(std::vector<uint8_t> &out, const EncodedNode &node) {
  const std::string *texts[] = {&node.id, &node.parent, &node.title,
                                &node.description, &node.target,
                                &node.binding_key, &node.value};
  uint32_t record_size = onion::plugin_ui::kNodeHeaderSize;
  for (const std::string *text : texts)
    record_size += static_cast<uint32_t>(text->size());
  put_u32(out, record_size);
  put_u16(out, static_cast<uint16_t>(node.kind));
  put_u16(out, 0);
  put_u32(out, node.flags);
  put_u32(out, static_cast<uint32_t>(node.value_type));
  put_u32(out, static_cast<uint32_t>(node.binding));
  put_u64(out, static_cast<uint64_t>(node.min_value));
  put_u64(out, static_cast<uint64_t>(node.max_value));
  put_u32(out, node.min_length);
  put_u32(out, node.max_length);
  for (const std::string *text : texts)
    put_u16(out, static_cast<uint16_t>(text->size()));
  put_u16(out, 0);
  for (const std::string *text : texts)
    out.insert(out.end(), text->begin(), text->end());
}

std::vector<uint8_t> make_document(std::string plugin_id = "TEST00001") {
  const std::string contribution = "settings";
  const std::string title = "Plugin & Settings";
  const std::string description = "Dynamic settings";
  const std::string root = "main";
  std::vector<EncodedNode> nodes = {
      {NodeKind::Page, 0, ValueType::None, BindingKind::None, 0, 0, 0, 0,
       "main", "", "Main", "", "", "", ""},
      {NodeKind::Page, 0, ValueType::None, BindingKind::None, 0, 0, 0, 0,
       "advanced", "", "Advanced", "", "", "", ""},
      {NodeKind::Toggle, 0, ValueType::Bool, BindingKind::Config, 0, 0, 0, 0,
       "enabled", "main", "Enabled", "Enable service", "", "enabled", "true"},
      {NodeKind::Menu, 0, ValueType::None, BindingKind::None, 0, 0, 0, 0,
       "advanced_menu", "main", "Advanced", "", "advanced", "", ""},
      {NodeKind::Input, 0, ValueType::Integer, BindingKind::Event, 1, 65535, 1, 5,
       "port", "advanced", "Port", "", "", "port_changed", "1337"},
      {NodeKind::List, 0, ValueType::String, BindingKind::Event, 0, 0, 0, 0,
       "mode", "advanced", "Mode", "", "", "mode_changed", "safe"},
      {NodeKind::ListItem, 0, ValueType::String, BindingKind::None, 0, 0, 0, 0,
       "safe", "mode", "Safe", "", "", "", "safe"},
      {NodeKind::Action, 0, ValueType::None, BindingKind::Event, 0, 0, 0, 0,
       "reload", "advanced", "Reload", "", "", "reload", ""},
  };

  std::vector<uint8_t> out;
  put_u32(out, onion::plugin_ui::kDocumentMagic);
  put_u16(out, onion::plugin_ui::kWireVersion);
  put_u16(out, onion::plugin_ui::kDocumentHeaderSize);
  put_u32(out, 0);
  put_u32(out, static_cast<uint32_t>(nodes.size()));
  put_u32(out, 0);
  put_u32(out, 10);
  const std::string *metadata[] = {&plugin_id, &contribution, &title,
                                   &description, &root};
  for (const std::string *text : metadata)
    put_u16(out, static_cast<uint16_t>(text->size()));
  put_u16(out, 0);
  for (const std::string *text : metadata)
    out.insert(out.end(), text->begin(), text->end());
  for (const EncodedNode &node : nodes) append_node(out, node);
  patch_u32(out, 8, static_cast<uint32_t>(out.size()));
  return out;
}

uint32_t checksum32(const std::vector<uint8_t> &bytes) {
  uint32_t checksum = 2166136261u;
  for (uint8_t byte : bytes) {
    checksum ^= byte;
    checksum *= 16777619u;
  }
  return checksum;
}

std::vector<uint8_t> make_hello(std::string_view owner,
                                uint32_t capabilities) {
  std::vector<uint8_t> payload;
  put_u32(payload, onion::plugin_session::kPluginAbiVersion);
  put_u32(payload, capabilities);
  put_u16(payload, static_cast<uint16_t>(owner.size()));
  put_u16(payload, 0);
  payload.insert(payload.end(), owner.begin(), owner.end());
  return payload;
}

onion::plugin_session::Frame make_frame(
    uint32_t request_id, uint16_t command,
    std::span<const uint8_t> payload = {}) {
  onion::plugin_session::Frame frame;
  frame.command = command;
  frame.request_id = request_id;
  frame.payload_size = static_cast<uint32_t>(payload.size());
  std::copy(payload.begin(), payload.end(), frame.payload);
  return frame;
}

onion::plugin_session::Frame round_trip(
    int socket, uint32_t request_id, uint16_t command,
    std::span<const uint8_t> payload = {}) {
  const onion::plugin_session::Frame request =
      make_frame(request_id, command, payload);
  onion::plugin_session::Frame response;
  const int sent = onion::ipc_network_send_full(
      socket, &request, static_cast<int32_t>(sizeof(request)));
  if (sent != static_cast<int>(sizeof(request))) return {};
  const int received = onion::ipc_network_recv_full(
      socket, &response, static_cast<int32_t>(sizeof(response)));
  return received == static_cast<int>(sizeof(response)) ? response
                                                        : onion::plugin_session::Frame{};
}

int32_t response_status(const onion::plugin_session::Frame &response) {
  return static_cast<int32_t>(read_u32(response.payload));
}

int test_registry_and_renderer(void) {
  onion::plugin_ui::Registry registry;
  const std::vector<uint8_t> encoded = make_document();
  auto registered = registry.register_encoded("TEST00001", encoded);
  TEST_ASSERT_EQ_INT(static_cast<int>(onion::plugin_ui::Status::Ok),
                     static_cast<int>(registered.status));
  TEST_ASSERT_TRUE(registered.handle != 0);

  const auto snapshot = registry.snapshot();
  TEST_ASSERT_EQ_U64(1, snapshot.generation);
  const auto *entry = snapshot.find(registered.handle);
  TEST_ASSERT_TRUE(entry != nullptr && entry->document != nullptr);

  const auto profile =
      onion::shellui::dynamic_ui::FirmwareProfile::for_system_version(0x09000000);
  auto rendered = onion::shellui::dynamic_ui::render_page(
      *entry->document, "main", profile);
  TEST_ASSERT_EQ_INT(
      static_cast<int>(onion::shellui::dynamic_ui::RenderStatus::Ok),
      static_cast<int>(rendered.status));
  TEST_ASSERT_TRUE(rendered.xml.find("Plugin &amp; Settings") == std::string::npos);
  TEST_ASSERT_TRUE(rendered.xml.find("title=\"Main\"") != std::string::npos);
  TEST_ASSERT_TRUE(rendered.xml.find("toggle_switch") != std::string::npos);
  TEST_ASSERT_TRUE(rendered.xml.find("value=\"1\"") != std::string::npos);
  TEST_ASSERT_TRUE(rendered.xml.find("onion_ui_") != std::string::npos);

  const std::string advanced_resource =
      onion::shellui::dynamic_ui::resource_name(*entry->document, "advanced");
  std::string page_id;
  const auto *resolved = onion::shellui::dynamic_ui::resolve_resource(
      snapshot, advanced_resource, &page_id);
  TEST_ASSERT_TRUE(resolved != nullptr);
  TEST_ASSERT_STREQ("advanced", page_id.c_str());

  const std::string toggle_id =
      onion::shellui::dynamic_ui::control_id(*entry->document, "enabled");
  const auto *toggle = onion::shellui::dynamic_ui::resolve_control(
      *entry->document, "main", toggle_id);
  TEST_ASSERT_TRUE(toggle != nullptr && toggle->kind == NodeKind::Toggle);

  onion::shellui::dynamic_ui::configure(0x09000000);
  onion::shellui::dynamic_ui::replace_snapshot(snapshot);
  ps5ui::Page plugin_page("plugins", "Plugins");
  onion::shellui::dynamic_ui::append_plugin_links(plugin_page);
  const std::string plugin_xml = plugin_page.build();
  TEST_ASSERT_TRUE(plugin_xml.find("Plugin &amp; Settings") != std::string::npos);
  std::string runtime_xml;
  const std::string root_resource =
      onion::shellui::dynamic_ui::resource_name(*entry->document, "main");
  TEST_ASSERT_TRUE(onion::shellui::dynamic_ui::render_resource(
      root_resource, runtime_xml));
  TEST_ASSERT_TRUE(onion::shellui::dynamic_ui::render_resource(
      advanced_resource, runtime_xml));
  onion::shellui::dynamic_ui::set_action_sink(action_sink, nullptr);
  g_action_received = false;
  const std::string reload_id =
      onion::shellui::dynamic_ui::control_id(*entry->document, "reload");
  TEST_ASSERT_EQ_INT(
      static_cast<int>(onion::shellui::dynamic_ui::DispatchResult::Accepted),
      static_cast<int>(onion::shellui::dynamic_ui::dispatch_control(reload_id, "")));
  TEST_ASSERT_TRUE(g_action_received);
  TEST_ASSERT_TRUE(onion::shellui::dynamic_ui::leave_active_page());
  TEST_ASSERT_TRUE(!onion::shellui::dynamic_ui::leave_active_page());

  TEST_ASSERT_EQ_INT(
      static_cast<int>(onion::plugin_ui::Status::Ok),
      static_cast<int>(registry.set_value("TEST00001", registered.handle,
                                          "enabled", ValueType::Bool, "false")));
  TEST_ASSERT_EQ_INT(
      static_cast<int>(onion::plugin_ui::Status::InvalidArgument),
      static_cast<int>(registry.set_value("TEST00001", registered.handle,
                                          "mode", ValueType::String, "missing")));
  TEST_ASSERT_EQ_U64(1, registry.remove_owner("TEST00001"));
  TEST_ASSERT_TRUE(registry.snapshot().contributions.empty());
  onion::shellui::dynamic_ui::replace_snapshot({});
  return 0;
}

int test_owner_and_encoding_validation(void) {
  onion::plugin_ui::Registry registry;
  auto spoofed = registry.register_encoded("OTHER0001", make_document());
  TEST_ASSERT_EQ_INT(
      static_cast<int>(onion::plugin_ui::Status::PermissionDenied),
      static_cast<int>(spoofed.status));

  std::vector<uint8_t> truncated = make_document();
  truncated.pop_back();
  onion::plugin_ui::Document document;
  TEST_ASSERT_EQ_INT(
      static_cast<int>(onion::plugin_ui::Status::InvalidEncoding),
      static_cast<int>(onion::plugin_ui::decode_document(truncated, document)));

  TEST_ASSERT_EQ_INT(
      static_cast<int>(onion::plugin_ui::Status::Ok),
      static_cast<int>(onion::plugin_ui::decode_document(make_document(), document)));
  std::vector<uint8_t> reencoded;
  TEST_ASSERT_EQ_INT(
      static_cast<int>(onion::plugin_ui::Status::Ok),
      static_cast<int>(onion::plugin_ui::encode_document(document, reencoded)));
  TEST_ASSERT_TRUE(reencoded == make_document());
  return 0;
}

int test_bridge_protocol_and_actions(void) {
  using namespace onion;
  plugin_ui::Registry registry;
  const auto registered = registry.register_encoded("TEST00001", make_document());
  TEST_ASSERT_EQ_INT(static_cast<int>(plugin_ui::Status::Ok),
                     static_cast<int>(registered.status));

  std::vector<uint8_t> snapshot_bytes;
  TEST_ASSERT_TRUE(plugin_bridge::encode_snapshot(registry.snapshot(),
                                                   snapshot_bytes));
  plugin_ui::RegistrySnapshot decoded;
  TEST_ASSERT_TRUE(plugin_bridge::decode_snapshot(snapshot_bytes, decoded));
  TEST_ASSERT_EQ_U64(registry.snapshot().generation, decoded.generation);
  TEST_ASSERT_EQ_U64(1, decoded.contributions.size());
  TEST_ASSERT_STREQ("TEST00001", decoded.contributions[0].owner.c_str());

  uint8_t header_bytes[plugin_bridge::kFrameHeaderSize];
  TEST_ASSERT_TRUE(plugin_bridge::encode_frame_header(
      {plugin_bridge::MessageType::Snapshot,
       static_cast<uint32_t>(snapshot_bytes.size())}, header_bytes));
  plugin_bridge::FrameHeader header;
  TEST_ASSERT_TRUE(plugin_bridge::decode_frame_header(header_bytes, header));
  TEST_ASSERT_EQ_U64(snapshot_bytes.size(), header.payload_size);
  header_bytes[12] = 1;
  TEST_ASSERT_TRUE(!plugin_bridge::decode_frame_header(header_bytes, header));

  plugin_bridge::ActionRequest action{registered.handle, "enabled", "false"};
  std::vector<uint8_t> action_bytes;
  TEST_ASSERT_TRUE(plugin_bridge::encode_action(action, action_bytes));
  plugin_bridge::ActionRequest decoded_action;
  TEST_ASSERT_TRUE(plugin_bridge::decode_action(action_bytes, decoded_action));
  TEST_ASSERT_STREQ("enabled", decoded_action.node_id.c_str());
  action_bytes.push_back(0);
  TEST_ASSERT_TRUE(!plugin_bridge::decode_action(action_bytes, decoded_action));

  plugin_ui::ProtocolBroker broker(registry);
  plugin_ui::ActionEvent event;
  TEST_ASSERT_EQ_INT(
      static_cast<int>(plugin_ui::Status::Ok),
      static_cast<int>(broker.dispatch_action(registered.handle, "enabled",
                                              "false", event)));
  TEST_ASSERT_STREQ("TEST00001", event.owner.c_str());
  TEST_ASSERT_STREQ("settings", event.contribution_id.c_str());
  TEST_ASSERT_STREQ("main", event.page_id.c_str());
  TEST_ASSERT_STREQ("enabled", event.node_id.c_str());
  TEST_ASSERT_STREQ("false", event.value.c_str());
  TEST_ASSERT_EQ_U64(1, event.sequence);
  const auto updated = registry.snapshot();
  const auto *entry = updated.find(registered.handle);
  TEST_ASSERT_TRUE(entry && entry->document);
  TEST_ASSERT_STREQ("false",
                    entry->document->find_node("enabled")->value.c_str());

  std::vector<uint8_t> event_bytes;
  TEST_ASSERT_TRUE(plugin_bridge::encode_ui_event(event, event_bytes));
  TEST_ASSERT_EQ_INT(plugin_bridge::kUiActionEventId,
                     read_u32(event_bytes));
  TEST_ASSERT_EQ_INT(
      static_cast<int>(plugin_ui::Status::InvalidArgument),
      static_cast<int>(broker.dispatch_action(registered.handle, "main", "",
                                              event)));
  return 0;
}

int test_unknown_firmware_fails_closed(void) {
  onion::plugin_ui::Document document;
  TEST_ASSERT_EQ_INT(
      static_cast<int>(onion::plugin_ui::Status::Ok),
      static_cast<int>(onion::plugin_ui::decode_document(make_document(), document)));
  const auto profile =
      onion::shellui::dynamic_ui::FirmwareProfile::for_system_version(0x13000000);
  const auto rendered =
      onion::shellui::dynamic_ui::render_page(document, "main", profile);
  TEST_ASSERT_EQ_INT(
      static_cast<int>(onion::shellui::dynamic_ui::RenderStatus::UnsupportedFirmware),
      static_cast<int>(rendered.status));
  return 0;
}

int test_registration_transaction_and_disconnect(void) {
  onion::plugin_ui::Registry registry;
  onion::plugin_ui::RegistrationBroker broker(registry);
  const std::vector<uint8_t> encoded = make_document();
  const auto begin = broker.begin("TEST00001", static_cast<uint32_t>(encoded.size()),
                                  checksum32(encoded));
  TEST_ASSERT_EQ_INT(static_cast<int>(onion::plugin_ui::Status::Ok),
                     static_cast<int>(begin.status));
  const size_t split = encoded.size() / 2;
  TEST_ASSERT_EQ_INT(
      static_cast<int>(onion::plugin_ui::Status::Stale),
      static_cast<int>(broker.append("TEST00001", begin.transfer_id, 1,
                                     std::span(encoded).first(split))));
  TEST_ASSERT_EQ_INT(
      static_cast<int>(onion::plugin_ui::Status::Ok),
      static_cast<int>(broker.append("TEST00001", begin.transfer_id, 0,
                                     std::span(encoded).first(split))));
  TEST_ASSERT_EQ_INT(
      static_cast<int>(onion::plugin_ui::Status::Ok),
      static_cast<int>(broker.append("TEST00001", begin.transfer_id,
                                     static_cast<uint32_t>(split),
                                     std::span(encoded).subspan(split))));
  const auto committed = broker.commit("TEST00001", begin.transfer_id);
  TEST_ASSERT_EQ_INT(static_cast<int>(onion::plugin_ui::Status::Ok),
                     static_cast<int>(committed.status));
  TEST_ASSERT_EQ_U64(1, broker.disconnect("TEST00001"));
  TEST_ASSERT_TRUE(registry.snapshot().contributions.empty());
  return 0;
}

int test_protocol_broker(void) {
  using onion::plugin_ui::ProtocolBroker;
  using onion::plugin_ui::Status;
  using onion::plugin_ui::WireCommand;

  onion::plugin_ui::Registry registry;
  ProtocolBroker broker(registry);
  broker.set_snapshot_sink(snapshot_sink, nullptr);
  g_published_generation = 0;

  const std::vector<uint8_t> encoded = make_document();
  std::vector<uint8_t> begin_payload;
  put_u32(begin_payload, static_cast<uint32_t>(encoded.size()));
  put_u32(begin_payload, checksum32(encoded));
  auto response = broker.dispatch(
      "TEST00001", static_cast<uint16_t>(WireCommand::RegisterBegin),
      begin_payload);
  TEST_ASSERT_EQ_INT(static_cast<int>(Status::Ok),
                     static_cast<int>(response.status));
  TEST_ASSERT_EQ_U64(4, response.data.size());
  const uint32_t transfer_id = read_u32(response.data);

  std::vector<uint8_t> chunk_payload;
  put_u32(chunk_payload, transfer_id);
  put_u32(chunk_payload, 0);
  chunk_payload.insert(chunk_payload.end(), encoded.begin(), encoded.end());
  response = broker.dispatch(
      "TEST00001", static_cast<uint16_t>(WireCommand::RegisterChunk),
      chunk_payload);
  TEST_ASSERT_EQ_INT(static_cast<int>(Status::Ok),
                     static_cast<int>(response.status));

  std::vector<uint8_t> commit_payload;
  put_u32(commit_payload, transfer_id);
  response = broker.dispatch(
      "TEST00001", static_cast<uint16_t>(WireCommand::RegisterCommit),
      commit_payload);
  TEST_ASSERT_EQ_INT(static_cast<int>(Status::Ok),
                     static_cast<int>(response.status));
  TEST_ASSERT_EQ_U64(8, response.data.size());
  const uint64_t handle = read_u64(response.data);
  TEST_ASSERT_TRUE(handle != 0 && g_published_generation == 1);

  std::vector<uint8_t> set_value_payload;
  put_u64(set_value_payload, handle);
  put_u32(set_value_payload, static_cast<uint32_t>(ValueType::Bool));
  put_u16(set_value_payload, 7);
  put_u16(set_value_payload, 5);
  const std::string update = "enabledfalse";
  set_value_payload.insert(set_value_payload.end(), update.begin(), update.end());
  response = broker.dispatch(
      "TEST00001", static_cast<uint16_t>(WireCommand::SetValue),
      set_value_payload);
  TEST_ASSERT_EQ_INT(static_cast<int>(Status::Ok),
                     static_cast<int>(response.status));
  TEST_ASSERT_EQ_U64(2, g_published_generation);

  std::vector<uint8_t> unregister_payload;
  put_u64(unregister_payload, handle);
  response = broker.dispatch(
      "OTHER0001", static_cast<uint16_t>(WireCommand::Unregister),
      unregister_payload);
  TEST_ASSERT_EQ_INT(static_cast<int>(Status::NotFound),
                     static_cast<int>(response.status));

  TEST_ASSERT_EQ_U64(1, broker.disconnect("TEST00001"));
  TEST_ASSERT_EQ_U64(3, g_published_generation);
  TEST_ASSERT_TRUE(registry.snapshot().contributions.empty());
  return 0;
}

int test_connection_session(void) {
  using onion::plugin_session::ConnectionSession;
  using onion::plugin_session::SessionDirectory;
  using onion::plugin_ui::ProtocolBroker;
  using onion::plugin_ui::Status;
  using onion::plugin_ui::WireCommand;

  onion::plugin_ui::Registry registry;
  ProtocolBroker broker(registry);
  SessionDirectory directory;
  TestEventSource events;
  ConnectionSession first(directory, broker, &events);

  std::vector<uint8_t> begin_payload;
  const std::vector<uint8_t> encoded = make_document();
  put_u32(begin_payload, static_cast<uint32_t>(encoded.size()));
  put_u32(begin_payload, checksum32(encoded));
  auto response = first.dispatch(
      static_cast<uint16_t>(WireCommand::RegisterBegin), begin_payload);
  TEST_ASSERT_EQ_INT(static_cast<int>(Status::InvalidState),
                     static_cast<int>(response.status));

  response = first.dispatch(onion::plugin_session::kHelloCommand,
                            make_hello("TEST00001", 1u << 31));
  TEST_ASSERT_EQ_INT(static_cast<int>(Status::InvalidArgument),
                     static_cast<int>(response.status));

  response = first.dispatch(
      onion::plugin_session::kHelloCommand,
      make_hello("TEST00001", onion::plugin_session::Ui));
  TEST_ASSERT_EQ_INT(static_cast<int>(Status::Ok),
                     static_cast<int>(response.status));
  TEST_ASSERT_TRUE(first.is_open());
  std::string first_owner = first.owner();
  TEST_ASSERT_STREQ("TEST00001", first_owner.c_str());
  TEST_ASSERT_EQ_U64(onion::plugin_session::Ui, first.capabilities());
  TEST_ASSERT_TRUE(directory.contains("TEST00001"));

  response = first.dispatch(onion::plugin_session::kEventCommand, {});
  TEST_ASSERT_EQ_INT(static_cast<int>(Status::NotFound),
                     static_cast<int>(response.status));
  TEST_ASSERT_STREQ("TEST00001", events.polled_owner.c_str());
  events.event = {1, 2, 3};
  response = first.dispatch(onion::plugin_session::kEventCommand, {});
  TEST_ASSERT_EQ_INT(static_cast<int>(Status::Ok),
                     static_cast<int>(response.status));
  TEST_ASSERT_EQ_U64(3, response.data.size());

  response = first.dispatch(
      onion::plugin_session::kHelloCommand,
      make_hello("CHANGED01", onion::plugin_session::Ui));
  TEST_ASSERT_EQ_INT(static_cast<int>(Status::InvalidState),
                     static_cast<int>(response.status));
  first_owner = first.owner();
  TEST_ASSERT_STREQ("TEST00001", first_owner.c_str());

  response = first.dispatch(99, {});
  TEST_ASSERT_EQ_INT(static_cast<int>(Status::NotSupported),
                     static_cast<int>(response.status));

  ConnectionSession duplicate(directory, broker);
  response = duplicate.dispatch(
      onion::plugin_session::kHelloCommand,
      make_hello("TEST00001", onion::plugin_session::Ui));
  TEST_ASSERT_EQ_INT(static_cast<int>(Status::Busy),
                     static_cast<int>(response.status));
  TEST_ASSERT_TRUE(!duplicate.is_open());

  ConnectionSession without_ui(directory, broker);
  response = without_ui.dispatch(
      onion::plugin_session::kHelloCommand,
      make_hello("OTHER0001", onion::plugin_session::Ipc));
  TEST_ASSERT_EQ_INT(static_cast<int>(Status::Ok),
                     static_cast<int>(response.status));
  response = without_ui.dispatch(
      static_cast<uint16_t>(WireCommand::RegisterBegin), begin_payload);
  TEST_ASSERT_EQ_INT(static_cast<int>(Status::PermissionDenied),
                     static_cast<int>(response.status));

  response = first.dispatch(
      static_cast<uint16_t>(WireCommand::RegisterBegin), begin_payload);
  TEST_ASSERT_EQ_INT(static_cast<int>(Status::Ok),
                     static_cast<int>(response.status));
  const uint32_t transfer_id = read_u32(response.data);
  std::vector<uint8_t> chunk_payload;
  put_u32(chunk_payload, transfer_id);
  put_u32(chunk_payload, 0);
  chunk_payload.insert(chunk_payload.end(), encoded.begin(), encoded.end());
  response = first.dispatch(
      static_cast<uint16_t>(WireCommand::RegisterChunk), chunk_payload);
  TEST_ASSERT_EQ_INT(static_cast<int>(Status::Ok),
                     static_cast<int>(response.status));
  std::vector<uint8_t> commit_payload;
  put_u32(commit_payload, transfer_id);
  response = first.dispatch(
      static_cast<uint16_t>(WireCommand::RegisterCommit), commit_payload);
  TEST_ASSERT_EQ_INT(static_cast<int>(Status::Ok),
                     static_cast<int>(response.status));
  const uint64_t handle = read_u64(response.data);
  TEST_ASSERT_TRUE(handle != 0);

  ConnectionSession other(directory, broker);
  response = other.dispatch(
      onion::plugin_session::kHelloCommand,
      make_hello("OTHER0002", onion::plugin_session::Ui));
  TEST_ASSERT_EQ_INT(static_cast<int>(Status::Ok),
                     static_cast<int>(response.status));
  std::vector<uint8_t> unregister_payload;
  put_u64(unregister_payload, handle);
  response = other.dispatch(static_cast<uint16_t>(WireCommand::Unregister),
                            unregister_payload);
  TEST_ASSERT_EQ_INT(static_cast<int>(Status::NotFound),
                     static_cast<int>(response.status));

  first.disconnect();
  TEST_ASSERT_TRUE(!first.is_open());
  TEST_ASSERT_TRUE(!directory.contains("TEST00001"));
  TEST_ASSERT_TRUE(registry.snapshot().contributions.empty());
  TEST_ASSERT_STREQ("TEST00001", events.disconnected_owner.c_str());

  ConnectionSession reconnected(directory, broker);
  response = reconnected.dispatch(
      onion::plugin_session::kHelloCommand,
      make_hello("TEST00001", onion::plugin_session::Ui));
  TEST_ASSERT_EQ_INT(static_cast<int>(Status::Ok),
                     static_cast<int>(response.status));
  TEST_ASSERT_EQ_U64(3, directory.size());
  return 0;
}

int test_connection_frame_protocol(void) {
  onion::plugin_ui::Registry registry;
  onion::plugin_ui::ProtocolBroker broker(registry);
  onion::plugin_session::SessionDirectory directory;
  onion::plugin_session::ConnectionProtocol protocol(directory, broker);

  auto result = protocol.handle(make_frame(
      1, onion::plugin_session::kPingCommand));
  TEST_ASSERT_EQ_INT(-2, response_status(result.response));
  TEST_ASSERT_TRUE(!result.close_connection);

  result = protocol.handle(make_frame(
      2, onion::plugin_session::kHelloCommand,
      make_hello("TEST00001", onion::plugin_session::Ui)));
  TEST_ASSERT_EQ_INT(0, response_status(result.response));
  TEST_ASSERT_EQ_INT(onion::plugin_session::kResponseCommand,
                     result.response.command);
  TEST_ASSERT_EQ_U64(2, result.response.request_id);

  result = protocol.handle(make_frame(
      3, onion::plugin_session::kPingCommand));
  TEST_ASSERT_EQ_INT(0, response_status(result.response));

  onion::plugin_session::Frame malformed =
      make_frame(4, onion::plugin_session::kPingCommand);
  malformed.magic = 0;
  result = protocol.handle(malformed);
  TEST_ASSERT_EQ_INT(-8, response_status(result.response));
  TEST_ASSERT_TRUE(result.close_connection);
  return 0;
}

int test_plugin_socket_connection(void) {
  onion::plugin_ui::Registry registry;
  onion::plugin_ui::ProtocolBroker broker(registry);
  onion::plugin_session::SessionDirectory directory;
  int sockets[2] = {-1, -1};
  TEST_ASSERT_EQ_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));

  std::jthread server([&] {
    onion::daemon::plugin_ipc::serve_connection(sockets[1], directory, broker);
    close(sockets[1]);
  });
  struct ClientSocketGuard {
    int &socket;
    ~ClientSocketGuard() {
      if (socket >= 0) {
        shutdown(socket, SHUT_RDWR);
        close(socket);
      }
    }
  } client_guard{sockets[0]};

  auto response = round_trip(
      sockets[0], 1, onion::plugin_session::kHelloCommand,
      make_hello("TEST00001", onion::plugin_session::Ui));
  TEST_ASSERT_EQ_INT(0, response_status(response));

  const std::vector<uint8_t> encoded = make_document();
  std::vector<uint8_t> begin_payload;
  put_u32(begin_payload, static_cast<uint32_t>(encoded.size()));
  put_u32(begin_payload, checksum32(encoded));
  response = round_trip(
      sockets[0], 2,
      static_cast<uint16_t>(onion::plugin_ui::WireCommand::RegisterBegin),
      begin_payload);
  TEST_ASSERT_EQ_INT(0, response_status(response));
  TEST_ASSERT_EQ_U64(12, response.payload_size);
  const uint32_t transfer_id = read_u32(response.payload + 8);

  std::vector<uint8_t> chunk_payload;
  put_u32(chunk_payload, transfer_id);
  put_u32(chunk_payload, 0);
  chunk_payload.insert(chunk_payload.end(), encoded.begin(), encoded.end());
  response = round_trip(
      sockets[0], 3,
      static_cast<uint16_t>(onion::plugin_ui::WireCommand::RegisterChunk),
      chunk_payload);
  TEST_ASSERT_EQ_INT(0, response_status(response));

  std::vector<uint8_t> commit_payload;
  put_u32(commit_payload, transfer_id);
  response = round_trip(
      sockets[0], 4,
      static_cast<uint16_t>(onion::plugin_ui::WireCommand::RegisterCommit),
      commit_payload);
  TEST_ASSERT_EQ_INT(0, response_status(response));
  TEST_ASSERT_TRUE(!registry.snapshot().contributions.empty());

  shutdown(sockets[0], SHUT_RDWR);
  close(sockets[0]);
  sockets[0] = -1;
  server.join();
  TEST_ASSERT_EQ_U64(0, directory.size());
  TEST_ASSERT_TRUE(registry.snapshot().contributions.empty());
  return 0;
}

} // namespace

extern "C" int test_plugin_ui_suite(void) {
  int failures = 0;
  failures += onion_test_run("plugin_ui_registry_renderer",
                             test_registry_and_renderer);
  failures += onion_test_run("plugin_ui_owner_encoding",
                             test_owner_and_encoding_validation);
  failures += onion_test_run("plugin_ui_bridge_protocol",
                             test_bridge_protocol_and_actions);
  failures += onion_test_run("plugin_ui_unknown_firmware",
                             test_unknown_firmware_fails_closed);
  failures += onion_test_run("plugin_ui_registration_transaction",
                             test_registration_transaction_and_disconnect);
  failures += onion_test_run("plugin_ui_protocol_broker",
                             test_protocol_broker);
  failures += onion_test_run("plugin_ui_connection_session",
                             test_connection_session);
  failures += onion_test_run("plugin_ui_connection_frame_protocol",
                             test_connection_frame_protocol);
  failures += onion_test_run("plugin_ui_socket_connection",
                             test_plugin_socket_connection);
  return failures;
}
