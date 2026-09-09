/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * External plugin start/stop progress page backed by a Legacy Settings user_custom Panel.
 * Uses MorpheusUpdateUI3.UpdateProgressPanel for native PS5 visual presentation.
 * The worker thread handles IPC and waits for dynamic UI registration.
 * UI3 widget creation and updates stay on the ShellUI thread.
 */

#include "plugin_progress.hpp"

#include "external_symbols.hpp"
#include "hooked_funcs.hpp"
#include "ps5_settings_ui.hpp"
#include "shellui_state.hpp"
#include "toolbox_i18n.hpp"
#include "toolbox_navigation.hpp"
#include "progress_auto_return.hpp"
#include "dynamic_ui_runtime.hpp"

#include <onion/ipc_client.hpp>
#include <onion/platform.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <ranges>
#include <string>
#include <time.h>
#include <unistd.h>

namespace {

constexpr std::string_view kCustomElementId = "id_plugin_progress_custom";

constexpr const char *kPuiUi3Namespace = "Sce.PlayStation.PUI.UI3";
constexpr const char *kUpdatePanelNamespace =
    "Sce.Vsh.ShellUI.Settings.Peripherals.MorpheusUpdateUI3";
constexpr const char *kUpdatePanelClass = "UpdateProgressPanel";

enum class ProgressState { Idle, Running, Ok, Error };

struct BoundWidget {
  MonoObject *object = nullptr;
  uint32_t handle = 0;
};

struct PluginProgress {
  std::mutex mu;
  ProgressState state = ProgressState::Idle;
  int progress = 0;
  int displayed_progress = 0;
  std::string plugin_id;
  std::string plugin_name;
  bool is_start = false;
  std::string phase;
  std::string error;
  BoundWidget page;
  BoundWidget host;
  BoundWidget panel;
  BoundWidget title_label;
  BoundWidget phase_label;
  BoundWidget percent_label;
  BoundWidget cancel_label;
  BoundWidget progress_bar;
  std::string applied_title;
  std::string applied_phase;
  std::string applied_percent;
  bool percent_applied = false;
  bool cancel_cleared = false;
  int applied_progress = -1;
  int applied_bar_status = -1;
};

namespace {

toolbox::ProgressAutoReturn g_plugin_auto_return({
    toolbox::Page::PluginProgress,
    800,  // success dwell ms
    2000  // error dwell ms
});

} // namespace

PluginProgress g_progress;
std::atomic<uint64_t> g_session_generation{0};

struct WorkerArgs {
  uint64_t generation = 0;
  std::string plugin_id;
  bool is_start = false;
};

MonoDomain *current_domain() {
  MonoDomain *domain = mono_domain_get ? mono_domain_get() : nullptr;
  return domain ? domain : Root_Domain;
}

void release_widget(BoundWidget &widget) {
  if (widget.handle && mono_gchandle_free)
    mono_gchandle_free(widget.handle);
  widget = {};
}

void release_bound_widgets_locked() {
  release_widget(g_progress.host);
  release_widget(g_progress.panel);
  release_widget(g_progress.title_label);
  release_widget(g_progress.phase_label);
  release_widget(g_progress.percent_label);
  release_widget(g_progress.cancel_label);
  release_widget(g_progress.progress_bar);
  g_progress.applied_title.clear();
  g_progress.applied_phase.clear();
  g_progress.applied_percent.clear();
  g_progress.percent_applied = false;
  g_progress.cancel_cleared = false;
  g_progress.applied_progress = -1;
  g_progress.applied_bar_status = -1;
}

void reset_progress_locked() {
  release_bound_widgets_locked();
  g_progress.state = ProgressState::Idle;
  g_progress.progress = 0;
  g_progress.displayed_progress = 0;
  g_progress.plugin_id.clear();
  g_progress.plugin_name.clear();
  g_progress.is_start = false;
  g_progress.phase.clear();
  g_progress.error.clear();
  g_progress.cancel_cleared = false;
  g_plugin_auto_return.reset();
  release_widget(g_progress.page);
}

bool session_is_current(uint64_t generation) {
  return g_session_generation.load(std::memory_order_acquire) == generation;
}

void advance_displayed_progress_locked() {
  if (g_progress.state != ProgressState::Running &&
      g_progress.state != ProgressState::Ok) {
    return;
  }
  if (g_progress.state == ProgressState::Ok) {
    g_progress.displayed_progress = 100;
    return;
  }
  const int target = std::clamp(g_progress.progress, 0, 100);
  if (g_progress.displayed_progress > target) {
    g_progress.displayed_progress = target;
  } else if (g_progress.displayed_progress < target) {
    ++g_progress.displayed_progress;
  }
}

struct Presentation {
  std::string title;
  std::string phase;
  std::string percent;
  int bar_progress = 0;
  int bar_status = 0;
};

Presentation presentation_locked() {
  Presentation view;
  view.title = g_progress.plugin_name.empty() ? g_progress.plugin_id : g_progress.plugin_name;

  switch (g_progress.state) {
  case ProgressState::Running: {
    if (g_progress.is_start) {
      if (g_progress.phase == "loading_ui") {
        view.phase = toolbox_i18n::tr("plugins.external.progress_starting_sub");
      } else {
        view.phase = toolbox_i18n::tr("plugins.external.progress_starting");
      }
    } else {
      if (g_progress.phase == "cleaning_ui") {
        view.phase = toolbox_i18n::tr("plugins.external.progress_stopping_sub");
      } else {
        view.phase = toolbox_i18n::tr("plugins.external.progress_stopping");
      }
    }
    view.bar_progress = g_progress.displayed_progress;
    view.percent = std::to_string(view.bar_progress) + '%';
    return view;
  }
  case ProgressState::Ok:
    view.phase = toolbox_i18n::tr("plugins.external.progress_completed");
    view.percent = "100%";
    view.bar_progress = 100;
    return view;
  case ProgressState::Error:
    view.phase = toolbox_i18n::tr("plugins.external.progress_failed");
    view.bar_progress = std::clamp(g_progress.progress, 0, 100);
    view.bar_status = 2;
    return view;
  case ProgressState::Idle:
  default:
    view.phase.clear();
    return view;
  }
}

MonoMethod *ui3_property_setter(const char *class_name, const char *name) {
  if (!pui_img || !class_name || !name || !mono_class_from_name ||
      !mono_class_get_property_from_name || !mono_property_get_set_method) {
    return nullptr;
  }

  MonoClass *klass =
      mono_class_from_name(pui_img, kPuiUi3Namespace, class_name);
  MonoProperty *property =
      klass ? mono_class_get_property_from_name(klass, name) : nullptr;
  return property ? mono_property_get_set_method(property) : nullptr;
}

MonoMethod *ui3_property_getter(const char *class_name, const char *name) {
  if (!pui_img || !class_name || !name || !mono_class_from_name ||
      !mono_class_get_property_from_name || !mono_property_get_get_method) {
    return nullptr;
  }

  MonoClass *klass =
      mono_class_from_name(pui_img, kPuiUi3Namespace, class_name);
  MonoProperty *property =
      klass ? mono_class_get_property_from_name(klass, name) : nullptr;
  return property ? mono_property_get_get_method(property) : nullptr;
}

bool invoke_ui3_setter(MonoObject *object, const char *class_name,
                       const char *name, void *value) {
  MonoMethod *setter = ui3_property_setter(class_name, name);
  if (!setter || !mono_runtime_invoke) {
    LOG_ERROR("plugin_progress_ui3: UI3.%s.%s setter not found",
              class_name ? class_name : "?", name ? name : "?");
    return false;
  }

  void *args[] = {value};
  MonoObject *exc = nullptr;
  mono_runtime_invoke(setter, object, args, &exc);
  if (exc) {
    LOG_ERROR("plugin_progress_ui3: %s setter threw", name);
    return false;
  }
  return true;
}

bool set_label_text(MonoObject *object, const std::string &text) {
  if (!mono_string_new)
    return false;
  MonoDomain *domain = current_domain();
  MonoString *value = domain ? mono_string_new(domain, text.c_str()) : nullptr;
  return value && invoke_ui3_setter(object, "Label", "Text", value);
}

bool set_progress_bar_progress(MonoObject *object, float value) {
  return invoke_ui3_setter(object, "ProgressBar", "Progress", &value);
}

bool set_progress_bar_status(MonoObject *object, int value) {
  return invoke_ui3_setter(object, "ProgressBar", "Status", &value);
}

bool read_progress_bar_progress(MonoObject *object, float &value) {
  MonoMethod *getter = ui3_property_getter("ProgressBar", "Progress");
  if (!getter || !mono_runtime_invoke || !mono_object_unbox)
    return false;

  MonoObject *exc = nullptr;
  MonoObject *boxed = mono_runtime_invoke(getter, object, nullptr, &exc);
  void *raw = (!exc && boxed) ? mono_object_unbox(boxed) : nullptr;
  if (!raw)
    return false;

  std::memcpy(&value, raw, sizeof(value));
  return true;
}

MonoObject *new_update_progress_panel() {
  if (!mono_class_from_name || !mono_object_new ||
      !mono_runtime_object_init) {
    return nullptr;
  }

  MonoImage *legacy_image = getDLLimage(legacy_dec.c_str());
  MonoClass *klass =
      legacy_image ? mono_class_from_name(legacy_image, kUpdatePanelNamespace,
                                          kUpdatePanelClass)
                   : nullptr;
  MonoDomain *domain = current_domain();
  MonoObject *object =
      (klass && domain) ? mono_object_new(domain, klass) : nullptr;
  if (object) {
    mono_runtime_object_init(object);
    LOG_DEBUG("plugin_progress_ui3: created %s.%s", kUpdatePanelNamespace,
              kUpdatePanelClass);
  } else {
    LOG_ERROR("plugin_progress_ui3: failed to create %s.%s",
              kUpdatePanelNamespace, kUpdatePanelClass);
  }
  return object;
}

MonoObject *widget_child_at(MonoObject *root, int index) {
  if (!pui_img || !root || index < 0 || !mono_class_from_name ||
      !mono_class_get_method_from_name || !mono_runtime_invoke) {
    return nullptr;
  }

  MonoClass *widget_class =
      mono_class_from_name(pui_img, kPuiUi3Namespace, "Widget");
  MonoMethod *get_child =
      widget_class
          ? mono_class_get_method_from_name(widget_class, "GetChildAt", 1)
          : nullptr;
  if (!get_child) {
    LOG_ERROR("plugin_progress_ui3: Widget.GetChildAt unavailable");
    return nullptr;
  }

  void *args[] = {&index};
  MonoObject *exc = nullptr;
  MonoObject *result = mono_runtime_invoke(get_child, root, args, &exc);
  if (exc) {
    LOG_ERROR("plugin_progress_ui3: GetChildAt(%d) threw", index);
    return nullptr;
  }
  return result;
}

int widget_children_count(MonoObject *widget) {
  if (!pui_img || !widget || !mono_class_from_name ||
      !mono_class_get_property_from_name || !mono_property_get_get_method ||
      !mono_runtime_invoke || !mono_object_unbox) {
    return -1;
  }

  MonoClass *widget_class =
      mono_class_from_name(pui_img, kPuiUi3Namespace, "Widget");
  MonoProperty *property = widget_class
                               ? mono_class_get_property_from_name(
                                     widget_class, "ChildrenCount")
                               : nullptr;
  MonoMethod *getter = property ? mono_property_get_get_method(property) : nullptr;
  if (!getter)
    return -1;

  MonoObject *exc = nullptr;
  MonoObject *boxed = mono_runtime_invoke(getter, widget, nullptr, &exc);
  int *value = (!exc && boxed) ? static_cast<int *>(mono_object_unbox(boxed))
                               : nullptr;
  return value ? *value : -1;
}

bool pin_widget(BoundWidget &bound, MonoObject *object, const char *name) {
  if (!object || !mono_gchandle_new)
    return false;
  bound.handle = mono_gchandle_new(object, 1);
  if (!bound.handle) {
    LOG_ERROR("plugin_progress_ui3: failed to root %s", name);
    return false;
  }
  bound.object = object;
  return true;
}

MonoMethod *widget_append_child_method() {
  static MonoMethod *method = nullptr;
  if (method)
    return method;
  if (!pui_img || !mono_class_from_name || !mono_method_desc_new ||
      !mono_method_desc_search_in_class || !mono_method_desc_free) {
    return nullptr;
  }

  MonoClass *widget_class =
      mono_class_from_name(pui_img, kPuiUi3Namespace, "Widget");
  MonoMethodDesc *description = mono_method_desc_new(
      ":AppendChild(Sce.PlayStation.PUI.UI3.Widget)", 1);
  if (widget_class && description)
    method = mono_method_desc_search_in_class(description, widget_class);
  if (description)
    mono_method_desc_free(description);

  if (!method)
    LOG_ERROR("plugin_progress_ui3: exact Widget.AppendChild(Widget) missing");
  return method;
}

bool append_panel(MonoObject *widget, MonoObject *panel) {
  MonoMethod *append = widget_append_child_method();
  if (!widget || !panel || !append || !mono_runtime_invoke)
    return false;

  void *args[] = {panel};
  MonoObject *exc = nullptr;
  mono_runtime_invoke(append, widget, args, &exc);
  if (exc)
    LOG_ERROR("plugin_progress_ui3: Widget.AppendChild(Widget) threw");
  return exc == nullptr;
}

void apply_progress_locked() {
  if (!g_progress.progress_bar.object)
    return;

  advance_displayed_progress_locked();
  const Presentation view = presentation_locked();

  if (g_progress.title_label.object && g_progress.applied_title != view.title) {
    if (set_label_text(g_progress.title_label.object, view.title))
      g_progress.applied_title = view.title;
  }
  if (g_progress.phase_label.object && g_progress.applied_phase != view.phase) {
    if (set_label_text(g_progress.phase_label.object, view.phase))
      g_progress.applied_phase = view.phase;
  }
  if (g_progress.percent_label.object &&
      (!g_progress.percent_applied || g_progress.applied_percent != view.percent)) {
    if (set_label_text(g_progress.percent_label.object, view.percent)) {
      g_progress.applied_percent = view.percent;
      g_progress.percent_applied = true;
    }
  }
  if (g_progress.cancel_label.object && !g_progress.cancel_cleared) {
    if (set_label_text(g_progress.cancel_label.object, ""))
      g_progress.cancel_cleared = true;
  }

  const float normalized = static_cast<float>(view.bar_progress) / 100.0f;
  float actual = 0.0f;
  const bool readback =
      read_progress_bar_progress(g_progress.progress_bar.object, actual);
  const bool differs =
      !readback || !std::isfinite(actual) ||
      std::abs(actual - normalized) > 0.001f;
  const bool needs_write =
      readback ? differs : (g_progress.applied_progress != view.bar_progress);
  if (needs_write &&
      set_progress_bar_progress(g_progress.progress_bar.object, normalized)) {
    g_progress.applied_progress = view.bar_progress;
  } else if (readback && g_progress.applied_progress != view.bar_progress) {
    g_progress.applied_progress = view.bar_progress;
  }
  if (g_progress.applied_bar_status != view.bar_status) {
    if (set_progress_bar_status(g_progress.progress_bar.object,
                                view.bar_status))
      g_progress.applied_bar_status = view.bar_status;
  }
}

bool build_and_append_panel_locked(MonoObject *widget) {
  if (g_progress.host.object == widget && g_progress.panel.object) {
    apply_progress_locked();
    return true;
  }

  release_bound_widgets_locked();

  MonoObject *panel = new_update_progress_panel();
  if (!panel) {
    LOG_ERROR("plugin_progress_ui3: official progress panel unavailable");
    return false;
  }

  if (!pin_widget(g_progress.host, widget, "host widget") ||
      !pin_widget(g_progress.panel, panel, "panel")) {
    release_bound_widgets_locked();
    return false;
  }

  if (!append_panel(widget, panel)) {
    LOG_ERROR("plugin_progress_ui3: Widget.AppendChild(Widget) failed");
    release_bound_widgets_locked();
    return false;
  }

  const int children_count = widget_children_count(panel);
  LOG_DEBUG("plugin_progress_ui3: official panel children=%d", children_count);
  if (children_count < 5) {
    LOG_ERROR("plugin_progress_ui3: official panel has too few children");
    release_bound_widgets_locked();
    return false;
  }

  MonoObject *title = widget_child_at(panel, 0);
  MonoObject *phase = widget_child_at(panel, 1);
  MonoObject *bar = widget_child_at(panel, 2);
  MonoObject *percent = widget_child_at(panel, 3);
  MonoObject *cancel = widget_child_at(panel, 4);

  if (!pin_widget(g_progress.title_label, title, "title") ||
      !pin_widget(g_progress.phase_label, phase, "phase") ||
      !pin_widget(g_progress.progress_bar, bar, "bar") ||
      !pin_widget(g_progress.percent_label, percent, "percent") ||
      !pin_widget(g_progress.cancel_label, cancel, "cancel")) {
    release_bound_widgets_locked();
    return false;
  }

  apply_progress_locked();
  LOG_DEBUG("plugin_progress_ui3: official progress panel appended");
  return true;
}

void *plugin_progress_worker(void *raw) {
  std::unique_ptr<WorkerArgs> args(static_cast<WorkerArgs *>(raw));
  const uint64_t generation = args ? args->generation : 0;
  const std::string plugin_id = args ? args->plugin_id : "";
  const bool is_start = args ? args->is_start : false;

  {
    std::lock_guard<std::mutex> lock(g_progress.mu);
    if (!session_is_current(generation)) return nullptr;
    g_progress.progress = 20;
  }

  IPC_Client &ipc = IPC_Client::getInstance(false);
  const bool ok = is_start ? ipc.StartPlugin(plugin_id)
                           : ipc.StopPlugin(plugin_id);

  if (!session_is_current(generation)) return nullptr;

  if (!ok) {
    std::lock_guard<std::mutex> lock(g_progress.mu);
    if (!session_is_current(generation)) return nullptr;
    g_progress.state = ProgressState::Error;
    g_progress.error = "operation_failed";
    LOG_ERROR("plugin_progress: IPC failed for %s is_start=%d", plugin_id.c_str(), is_start);
    return nullptr;
  }

  {
    auto it = std::ranges::find(g_ui.external_plugins, plugin_id,
                                &PluginInventoryItem::plugin_id);
    if (it != g_ui.external_plugins.end()) {
      it->running = is_start;
    }
  }

  if (is_start) {
    {
      std::lock_guard<std::mutex> lock(g_progress.mu);
      if (!session_is_current(generation)) return nullptr;
      g_progress.progress = 55;
      g_progress.phase = "loading_ui";
    }

    constexpr int kWaitStepMs = 100;
    constexpr int kMaxSteps = 25; // 2.5s
    for (int step = 0; step < kMaxSteps; ++step) {
      usleep(kWaitStepMs * 1000);
      if (!session_is_current(generation)) return nullptr;

      const auto links = onion::shellui::dynamic_ui::plugin_settings_links();
      const bool found = std::ranges::any_of(links, [&](const auto &link) {
        return link.plugin_id == plugin_id;
      });

      if (found) {
        LOG_DEBUG("plugin_progress: dynamic UI link arrived after %d ms", (step + 1) * kWaitStepMs);
        break;
      }

      {
        std::lock_guard<std::mutex> lock(g_progress.mu);
        if (!session_is_current(generation)) return nullptr;
        if (g_progress.progress < 90)
          g_progress.progress += 1;
      }
    }
  } else {
    {
      std::lock_guard<std::mutex> lock(g_progress.mu);
      if (!session_is_current(generation)) return nullptr;
      g_progress.progress = 75;
      g_progress.phase = "cleaning_ui";
    }
    usleep(300 * 1000);
  }

  {
    std::lock_guard<std::mutex> lock(g_progress.mu);
    if (!session_is_current(generation)) return nullptr;
    g_progress.progress = 100;
    g_progress.state = ProgressState::Ok;
    LOG_DEBUG("plugin_progress: finished successfully for %s", plugin_id.c_str());
  }

  return nullptr;
}

} // namespace

bool plugin_progress_open_page(void) {
  return toolbox_push_resource("plugin_progress.xml");
}

void plugin_progress_show(std::string_view plugin_id, bool start) {
  if (!shellui_hooks_are_ready()) {
    LOG_ERROR("plugin_progress: hooks not ready");
    return;
  }

  std::string display_name(plugin_id);
  auto it = std::ranges::find(g_ui.external_plugins, plugin_id,
                              &PluginInventoryItem::plugin_id);
  if (it != g_ui.external_plugins.end() && !it->name.empty()) {
    display_name = it->name;
  }

  const uint64_t generation =
      g_session_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
  {
    std::lock_guard<std::mutex> lock(g_progress.mu);
    reset_progress_locked();
    g_progress.state = ProgressState::Running;
    g_progress.plugin_id = plugin_id;
    g_progress.plugin_name = display_name;
    g_progress.is_start = start;
    g_progress.progress = 10;
    g_progress.displayed_progress = 0;
    g_progress.phase = start ? "starting" : "stopping";
  }

  auto *args = new WorkerArgs{generation, std::string(plugin_id), start};
  pthread_t thread;
  if (pthread_create(&thread, nullptr, plugin_progress_worker, args) != 0) {
    delete args;
    std::lock_guard<std::mutex> lock(g_progress.mu);
    if (session_is_current(generation)) {
      g_progress.state = ProgressState::Error;
      g_progress.error = "worker_thread";
    }
    LOG_ERROR("plugin_progress: pthread_create failed");
    return;
  }
  pthread_detach(thread);
}

void generate_plugin_progress_xml(std::string &xml_buffer) {
  std::string title;
  {
    std::lock_guard<std::mutex> lock(g_progress.mu);
    title = g_progress.plugin_name.empty()
                ? toolbox_i18n::tr("plugins.external.progress_title")
                : g_progress.plugin_name;
  }
  ps5ui::Page page("id_plugin_progress", title);
  page.root_list_size("1340,740")
      .root_restorable(false)
      .user_custom(std::string(kCustomElementId));
  xml_buffer = page.build();
  LOG_DEBUG("plugin_progress_xml: generated %zu bytes", xml_buffer.size());
}

void plugin_progress_attach_panel(std::string_view id, MonoObject *widget) {
  if (id != kCustomElementId || !widget)
    return;
  std::lock_guard<std::mutex> lock(g_progress.mu);
  (void)build_and_append_panel_locked(widget);
}

void plugin_progress_bind_page(MonoObject *page) {
  if (!page)
    return;
  std::lock_guard<std::mutex> lock(g_progress.mu);
  if (g_progress.page.object == page)
    return;
  release_widget(g_progress.page);
  (void)pin_widget(g_progress.page, page, "page");
}

bool plugin_progress_handle_popping(MonoObject *outgoing) {
  std::lock_guard<std::mutex> lock(g_progress.mu);
  if (!outgoing || outgoing != g_progress.page.object)
    return false;

  g_session_generation.fetch_add(1, std::memory_order_acq_rel);
  reset_progress_locked();
  return true;
}

void shellui_poll_plugin_progress(void) {
  toolbox::ProgressOutcome outcome = toolbox::ProgressOutcome::Ongoing;
  {
    std::lock_guard<std::mutex> lock(g_progress.mu);
    apply_progress_locked();

    if (g_progress.state == ProgressState::Ok) {
      outcome = toolbox::ProgressOutcome::Success;
    } else if (g_progress.state == ProgressState::Error) {
      outcome = toolbox::ProgressOutcome::Failed;
    }
  }

  g_plugin_auto_return.poll(outcome);
}



