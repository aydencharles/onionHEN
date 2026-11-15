/* Copyright (C) 2025 OnionHEN / LightningMods — OnPress payloads / auto-start */
#include "onpress.hpp"
#include "shellui_payload_state.hpp"
#include "settings_page_refresh.hpp"
#include "toolbox_helpers.hpp"
#include "toolbox_i18n.hpp"
#include "toolbox_navigation.hpp"
#include <onion/payload.h>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <pthread.h>
#include <mutex>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

void *load_payload_thread(void *args);

namespace {

constexpr const char *kRunPrefix = "id_payload_run_";
constexpr const char *kAutoStartPrefix = "id_payload_autostart_";
constexpr const char *kPriorityPrefix = "id_payload_priority_";
constexpr const char *kDelayPrefix = "id_payload_delay_";
constexpr const char *kDeletePrefix = "id_payload_delete_";
std::mutex launching_mutex;
std::set<std::string> launching_payloads;

PayloadEntry *payload_for_control(const std::string &control_id,
                                  const char *prefix) {
  const std::string prefix_string(prefix);
  if (control_id.rfind(prefix_string, 0) != 0)
    return nullptr;
  const std::string id = control_id.substr(prefix_string.size());
  for (auto &entry : g_ui.payloads_list)
    if (entry.id == id)
      return &entry;
  return nullptr;
}

bool control_enabled(const std::string &value) {
  return value == "1" || value == "true";
}

bool payload_busy(const PayloadEntry &entry) {
  std::lock_guard lock(launching_mutex);
  return launching_payloads.contains(entry.id) ||
      onion_payload_path_running(entry.shellui_path.c_str());
}

void *launch_payload(void *args) {
  const std::string id = static_cast<PayloadEntry *>(args)->id;
  load_payload_thread(args);
  std::lock_guard lock(launching_mutex);
  launching_payloads.erase(id);
  return nullptr;
}

void start_payload(const PayloadEntry &entry) {
  {
    std::lock_guard lock(launching_mutex);
    if (!launching_payloads.insert(entry.id).second)
      return;
  }
  pthread_t thread;
  auto *info = new PayloadEntry(entry);
  if (pthread_create(&thread, nullptr, launch_payload, info) == 0) {
    pthread_detach(thread);
    return;
  }
  delete info;
  {
    std::lock_guard lock(launching_mutex);
    launching_payloads.erase(entry.id);
  }
  notify("notify.payload.launch_failed", entry.path.c_str(), entry.tid.c_str());
}

} // namespace

void bind_payload_delete_control(MonoObject *element, const std::string &id) {
  const PayloadEntry *entry = payload_for_control(id, kDeletePrefix);
  if (entry && !onion::shellui::settings::set_enabled(
                   element, toolbox::has_real_payload_directory(entry->shellui_path) &&
                                !payload_busy(*entry)))
    LOG_ERROR("Failed to bind payload removal control");
}

static OnPressResult prefix_id_payload_run(OnPressContext &ctx) {
  if (ctx.id.rfind(kRunPrefix, 0) != 0) {
    return OnPressResult::NotMine;
  }
  PayloadEntry *entry = payload_for_control(ctx.id, kRunPrefix);
  if (!entry)
    return OnPressResult::Consumed;

  char pid_path[256];
  const int pid = static_cast<int>(shellui_payload_resolve_recorded_pid(
      entry->tid.c_str(), pid_path, sizeof(pid_path)));
  if (!control_enabled(ctx.value) && pid > 1) {
    LOG_DEBUG("killing recorded payload pid: %d (%s)", pid,
              entry->tid.c_str());
    IPC_Client::getInstance(false).ForceKillPID(pid);
    if (!onion_proc_is_alive(pid)) {
      unlink(pid_path);
      notify("notify.process.killed", entry->name.c_str());
    }
  } else if (control_enabled(ctx.value) && pid <= 1) {
    LOG_DEBUG("Payload %s not running", entry->tid.c_str());
    start_payload(*entry);
  }
  if (toolbox::is_installed_payload_path(entry->shellui_path))
    onion::shellui::settings::reset_element(
        ctx.instance, std::string(kDeletePrefix) + entry->id);
  return OnPressResult::Consumed;
}

static OnPressResult prefix_id_auto_payload(OnPressContext &ctx) {
  if (ctx.id.rfind(kAutoStartPrefix, 0) != 0) {
    return OnPressResult::NotMine;
  }
  PayloadEntry *entry = payload_for_control(ctx.id, kAutoStartPrefix);
  if (!entry)
    return OnPressResult::Consumed;

  const std::string auto_path = entry->shellui_path + ".auto_start";
  LOG_DEBUG("Auto start path: %s", auto_path.c_str());
  if (!control_enabled(ctx.value)) {
    unlink(auto_path.c_str());
  } else {
    const int fd = open(auto_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fd < 0)
      notify("notify.payload.autostart_file");
    else
      close(fd);
  }
  return OnPressResult::Consumed;
}

static OnPressResult set_payload_schedule(OnPressContext &ctx, bool priority) {
  ctx.dirty = false;
  PayloadEntry *entry = payload_for_control(
      ctx.id, priority ? kPriorityPrefix : kDelayPrefix);
  if (!entry)
    return OnPressResult::Consumed;

  OnionPayloadConfig config;
  onion_payload_config_load(entry->shellui_path.c_str(), &config);
  const int maximum = priority ? ONION_PAYLOAD_PRIORITY_MAX : ONION_PAYLOAD_DELAY_MAX;
  char *end = nullptr;
  errno = 0;
  const long value = std::strtol(ctx.value.c_str(), &end, 10);
  if (ctx.value.empty() || ctx.value.find_first_not_of("0123456789") !=
                               std::string::npos ||
      errno == ERANGE || !end || *end || value < 0 || value > maximum) {
    notify("notify.payload.config_invalid", maximum);
  } else {
    (priority ? config.priority : config.delay_seconds) = static_cast<int>(value);
    if (!onion_payload_config_save(entry->shellui_path.c_str(), &config))
      notify("notify.payload.config_save_failed", entry->name.c_str());
  }

  // Rebind the persisted value, including after rejected input or failed writes.
  onion_payload_config_load(entry->shellui_path.c_str(), &entry->config);
  const std::string saved = std::to_string(
      priority ? entry->config.priority : entry->config.delay_seconds);
  MonoDomain *domain = mono_domain_get ? mono_domain_get() : Root_Domain;
  if (!domain)
    domain = Root_Domain;
  if (ctx.element && domain && set_value_method && mono_string_new && mono_runtime_invoke) {
    MonoString *text = mono_string_new(domain, saved.c_str());
    if (text) {
      void *args[] = {text};
      MonoObject *exception = nullptr;
      mono_runtime_invoke(set_value_method, ctx.element, args, &exception);
      if (exception)
        LOG_ERROR("Failed to restore payload config control value");
    }
  }
  return OnPressResult::Consumed;
}

static OnPressResult delete_payload(OnPressContext &ctx) {
  ctx.dirty = false;
  const PayloadEntry *found = payload_for_control(ctx.id, kDeletePrefix);
  if (!found || !toolbox::is_installed_payload_path(found->shellui_path) ||
      !toolbox::has_real_payload_directory(found->shellui_path))
    return OnPressResult::Consumed;
  const PayloadEntry entry = *found;
  if (payload_busy(entry)) {
    notify("%s", toolbox_i18n::tr("payload.delete_running"));
    return OnPressResult::Consumed;
  }
  const bool removed = onion_payload_remove(entry.shellui_path.c_str());
  const int error = errno;
  if (!removed && error == EBUSY) {
    notify("%s", toolbox_i18n::tr("payload.delete_running"));
    return OnPressResult::Consumed;
  }
  const std::string message = toolbox_i18n::format(
      removed ? "payload.deleted_fmt" : "payload.delete_failed_fmt",
      entry.name.c_str());
  notify("%s", message.c_str());
  struct stat remaining {};
  if (removed || (lstat(entry.shellui_path.c_str(), &remaining) != 0 &&
                  errno == ENOENT))
    toolbox_pop_page();
  return OnPressResult::Consumed;
}

static const OnPressPrefixEntry kPrefix[] = {
    {kDeletePrefix, delete_payload},
    {kPriorityPrefix, +[](OnPressContext &ctx) {
       return set_payload_schedule(ctx, true);
     }},
    {kDelayPrefix, +[](OnPressContext &ctx) {
       return set_payload_schedule(ctx, false);
     }},
    {kAutoStartPrefix, prefix_id_auto_payload},
    {kRunPrefix, prefix_id_payload_run},
};

const OnPressPrefixEntry *onpress_payloads_prefix(size_t *count) {
  *count = sizeof(kPrefix) / sizeof(kPrefix[0]);
  return kPrefix;
}
