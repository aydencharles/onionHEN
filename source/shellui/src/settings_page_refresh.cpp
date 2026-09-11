#include "settings_page_refresh.hpp"

#include "external_symbols.hpp"
#include "hooked_funcs.hpp"
#include "settings_page_update.hpp"
#include "shellui_state.hpp"

#include <onion/platform.h>
#include <algorithm>
#include <map>
#include <memory>
#include <mutex>

namespace onion::shellui::settings {
namespace {

class Pinned {
public:
  explicit Pinned(MonoObject *object) : object_(object) {
    if (object && mono_gchandle_new && mono_gchandle_free)
      handle_ = mono_gchandle_new(object, 1);
  }
  ~Pinned() { if (handle_) mono_gchandle_free(handle_); }
  Pinned(const Pinned &) = delete;
  Pinned &operator=(const Pinned &) = delete;
  MonoObject *get() const { return handle_ ? object_ : nullptr; }
private:
  MonoObject *object_;
  uint32_t handle_ = 0;
};

MonoClass *klass(const char *name) {
  // All runtime view calls run on the UI thread in the ShellUI root domain.
  static MonoImage *image = nullptr;
  if (!image) image = getDLLimage(legacy_dec.c_str());
  return image && mono_class_from_name ? mono_class_from_name(
      image, "Sce.Vsh.ShellUI.Settings.CoreUI3", name) : nullptr;
}

MonoMethod *method(const char *owner, const char *name, int argc) {
  MonoClass *type = klass(owner);
  return type && mono_class_get_method_from_name
      ? mono_class_get_method_from_name(type, name, argc) : nullptr;
}

MonoMethod *string_overload(const char *name) {
  if (!mono_method_desc_new || !mono_method_desc_search_in_class ||
      !mono_method_desc_free) return nullptr;
  MonoClass *type = klass("SettingList");
  const std::string signature = std::string("SettingList:") + name + "(string)";
  MonoMethodDesc *desc = mono_method_desc_new(signature.c_str(), 0);
  MonoMethod *result = desc && type ? mono_method_desc_search_in_class(desc, type) : nullptr;
  if (desc) mono_method_desc_free(desc);
  return result;
}

bool invoke(MonoMethod *target, MonoObject *object, void **args,
            MonoObject **result = nullptr) {
  if (!target || !mono_runtime_invoke) return false;
  MonoObject *exception = nullptr;
  MonoObject *value = mono_runtime_invoke(target, object, args, &exception);
  if (exception) {
    LOG_ERROR("settings_refresh: managed invocation threw");
    return false;
  }
  if (result) *result = value;
  return true;
}

MonoObject *text(const std::string &value) {
  MonoDomain *domain = mono_domain_get ? mono_domain_get() : Root_Domain;
  if (!domain) domain = Root_Domain;
  return domain && mono_string_new
      ? reinterpret_cast<MonoObject *>(mono_string_new(domain, value.c_str())) : nullptr;
}

MonoObject *get(MonoObject *object, const char *owner, const char *getter) {
  MonoObject *result = nullptr;
  return object && invoke(method(owner, getter, 0), object, nullptr, &result)
      ? result : nullptr;
}

std::string id(MonoObject *object, const char *owner) {
  Pinned value(get(object, owner, "get_Id"));
  return value.get() ? Mono_to_String(reinterpret_cast<MonoString *>(value.get())) : "";
}

bool success_code(MonoMethod *target, MonoObject *object, void **args) {
  MonoObject *result = nullptr;
  if (!invoke(target, object, args, &result) || !result || !mono_object_unbox)
    return false;
  const int *code = static_cast<int *>(mono_object_unbox(result));
  if (!code || *code != 0) {
    LOG_ERROR("settings_refresh: firmware error %d", code ? *code : -1);
    return false;
  }
  return true;
}

class LegacyPageView final : public PageView {
public:
  explicit LegacyPageView(MonoObject *page) : page_(page) {}

  bool ready() const {
    MonoClass *data = klass("ElementData");
    return mono_field_get_value && mono_class_get_field_from_name && data &&
        mono_class_get_field_from_name(data, "attributes") &&
        string_overload("RemoveElement") && string_overload("ResetElement") &&
        method("SettingList", "AddElement", 3) && method("ElementData", "Create", 1) &&
        method("SettingList", "set_Title", 1) && method("SettingList", "ResetHeader", 0) &&
        method("AttributeList", "set_Item", 2) && method("SettingElement", "set_Value", 1);
  }

  bool set_title(const std::string &title) override {
    Pinned value(text(title));
    void *args[] = {value.get()};
    return value.get() && invoke(method("SettingList", "set_Title", 1), page_, args) &&
        invoke(method("SettingList", "ResetHeader", 0), page_, nullptr);
  }

  bool remove(const std::string &key) override {
    Pinned name(text(key));
    void *args[] = {name.get()};
    return name.get() && invoke(string_overload("RemoveElement"), page_, args);
  }

  bool insert(const ps5ui::Node &node, const std::string &before) override {
    Pinned tag(text(ps5ui::node_tag(node.kind)));
    MonoObject *created = nullptr;
    void *create_args[] = {tag.get()};
    if (!tag.get() || !invoke(method("ElementData", "Create", 1), nullptr,
                             create_args, &created)) return false;
    Pinned data(created);
    if (!data.get() || !attributes(data.get(), {}, node)) return false;
    Pinned position(before.empty() ? nullptr : text(before));
    bool insert_before = !before.empty();
    void *args[] = {data.get(), position.get(), &insert_before};
    if (!before.empty() && !position.get()) return false;
    const bool added = success_code(method("SettingList", "AddElement", 3), page_, args);
    if (added && update(node, node)) return true;
    // AddElement inserts before running callbacks, which can return an error.
    // Remove the failed insertion so the reconciler can retry without duplicates.
    remove(node.attrs.id);
    return false;
  }

  bool update(const ps5ui::Node &previous, const ps5ui::Node &next) override {
    Pinned name(text(next.attrs.id));
    void *args[] = {name.get()};
    MonoObject *found = nullptr;
    if (!name.get() || !invoke(method("SettingList", "get_Item", 1), page_, args, &found))
      return false;
    Pinned element(found);
    Pinned data(get(element.get(), "SettingElement", "get_Data"));
    if (!data.get() || !attributes(data.get(), previous, next)) return false;
    if (next.attrs.value || previous.attrs.value) {
      Pinned value(text(next.attrs.value.value_or("")));
      void *value_args[] = {value.get()};
      if (!value.get() || !invoke(method("SettingElement", "set_Value", 1),
                                 element.get(), value_args)) return false;
    }
    return success_code(string_overload("ResetElement"), page_, args);
  }

private:
  bool attributes(MonoObject *data, const ps5ui::Node &previous,
                  const ps5ui::Node &next) {
    MonoClassField *field = mono_class_get_field_from_name(klass("ElementData"), "attributes");
    MonoObject *object = nullptr;
    mono_field_get_value(data, field, &object);
    Pinned attributes(object);
    if (!attributes.get()) return false;
    const auto desired = ps5ui::node_attributes(next);
    auto writes = desired;
    for (const auto &[key, value] : ps5ui::node_attributes(previous)) {
      (void)value;
      if (std::none_of(desired.begin(), desired.end(),
          [&](const auto &entry) { return entry.first == key; }))
        // AttributeList ignores null writes; empty text clears display attributes.
        writes.emplace_back(key, "");
    }
    for (const auto &[key, value] : writes) {
      Pinned key_text(text(key));
      Pinned value_text(text(value));
      void *args[] = {key_text.get(), value_text.get()};
      if (!key_text.get() || !value_text.get() ||
          !invoke(method("AttributeList", "set_Item", 2), attributes.get(), args))
        return false;
    }
    return true;
  }
  MonoObject *page_;
};

struct Pending {
  ps5ui::Node model;
  PageSource source;
};
struct Bound {
  Bound(MonoObject *page, Pending pending)
      : owner(page), content(std::move(pending)), route(g_ui.active_page),
        plugin(g_ui.active_plugin) {}
  Pinned owner;
  Pending content;
  toolbox::Page route;
  std::string plugin;
};
std::mutex bindings_mutex;
std::map<std::string, Pending> pending_pages;
std::map<MonoObject *, std::shared_ptr<Bound>> pages;

} // namespace

std::string publish(const ps5ui::Node &model, PageSource source) {
  if (oSettingPageOnActivated && oSettingListCleanup && valid_live_model(model)) {
    std::lock_guard lock(bindings_mutex);
    pending_pages.insert_or_assign(model.attrs.id, Pending{model, std::move(source)});
  } else {
    LOG_WARN("settings_refresh: cannot register %s (hooks unavailable or unsupported model)",
             model.attrs.id.c_str());
  }
  return ps5ui::build_document(model);
}

void bind(MonoObject *page) {
  if (!page) return;
  {
    std::lock_guard lock(bindings_mutex);
    if (pages.contains(page) || pending_pages.empty()) return;
  }
  const auto key = id(page, "SettingList");
  std::lock_guard lock(bindings_mutex);
  auto found = pending_pages.find(key);
  if (found == pending_pages.end()) return;
  auto bound = std::make_shared<Bound>(page, std::move(found->second));
  pending_pages.erase(found);
  if (bound->owner.get()) {
    pages.emplace(page, std::move(bound));
    LOG_DEBUG("settings_refresh: bound %s page=%p", key.c_str(), static_cast<void *>(page));
  }
}

void refresh(MonoObject *page) {
  std::shared_ptr<Bound> bound;
  {
    std::lock_guard lock(bindings_mutex);
    auto found = pages.find(page);
    if (found == pages.end()) return;
    bound = found->second;
  }
  Pinned manager(get(page, "SettingList", "get_UIManager"));
  Pinned stack(get(manager.get(), "UIManager", "get_PageStack"));
  Pinned current(get(stack.get(), "SettingPageStack", "get_Current"));
  if (current.get() != page) return;
  // Bindings during Reset must resolve values in the returning page's context.
  g_ui.active_page = bound->route;
  g_ui.active_plugin = bound->plugin;
  LegacyPageView view(page);
  ps5ui::Node next;
  if (!view.ready() || !bound->content.source(next)) {
    LOG_ERROR("settings_refresh: source or firmware API unavailable for %s",
              bound->content.model.attrs.id.c_str());
    return;
  }
  Pinned focused(get(page, "SettingList", "GetFocusedItem"));
  const std::string focus = id(focused.get(), "SettingElement");
  const bool ok = reconcile(view, bound->content.model, next);
  if (!focus.empty() && std::any_of(bound->content.model.children.begin(),
      bound->content.model.children.end(), [&](const auto &node) {
        return node.attrs.id == focus;
      })) {
    Pinned name(text(focus));
    void *args[] = {name.get()};
    if (name.get()) invoke(method("SettingList", "SetFocusTo", 1), page, args);
  }
  LOG_DEBUG("settings_refresh: %s %s", bound->content.model.attrs.id.c_str(),
            ok ? "updated" : "incomplete; retained applied snapshot");
}

void release(MonoObject *page) {
  std::lock_guard lock(bindings_mutex);
  pages.erase(page);
}

} // namespace onion::shellui::settings
