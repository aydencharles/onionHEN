#include "cheats/i_cheat_parser.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "cheats/cheat_engine_internal.h"
#include "onion_cjson.hpp"

namespace onion::cheats {
namespace {

bool copyStringItem(const cJSON *object, const char *key, char *out,
                    size_t out_size) {
  const char *value = onion_cjson::string_item(object, key);
  if (!value || out_size == 0) {
    return false;
  }
  onion_cheat_copy_utf8(out, out_size, value);
  return true;
}

bool parseMemoryObject(const cJSON *object, onion_patch_t *patch) {
  const cJSON *offset = onion_cjson::item(object, "offset");
  const char *on = onion_cjson::string_item(object, "on");
  const char *off = onion_cjson::string_item(object, "off");
  if (!cJSON_IsObject(object) || !offset || !on || !off) {
    return false;
  }

  std::memset(patch, 0, sizeof(*patch));
  patch->offset = std::strtoull(
      onion_cjson::scalar_string(offset).c_str(), nullptr, 16);
  if (onion_cheat_hex_decode(on, patch->on, sizeof(patch->on),
                             &patch->on_len) < 0 ||
      onion_cheat_hex_decode(off, patch->off, sizeof(patch->off),
                             &patch->off_len) < 0) {
    return false;
  }

  const cJSON *section = onion_cjson::item(object, "section");
  if (section) {
    const int value = onion_cjson::int_value(section);
    if (value >= 0 && value < MODULE_INFO_MAX_SECTIONS) {
      patch->section = value;
    }
  }
  patch->absolute =
      onion_cjson::bool_value(onion_cjson::item(object, "absolute"));
  return true;
}

bool parseModObject(const cJSON *object, const char *process_name,
                    onion_cheat_entry_t *entry) {
  if (!cJSON_IsObject(object)) {
    return false;
  }
  std::memset(entry, 0, sizeof(*entry));
  if (!copyStringItem(object, "name", entry->name, sizeof(entry->name))) {
    return false;
  }
  const char *description = onion_cjson::string_item(object, "description", "");
  onion_cheat_copy_utf8(entry->description, sizeof(entry->description),
                        description);
  std::snprintf(entry->module_name, sizeof(entry->module_name), "%s",
                process_name);

  const cJSON *memory = onion_cjson::item(object, "memory");
  if (!memory) {
    return true;
  }
  if (!cJSON_IsArray(memory)) {
    return false;
  }

  cJSON *patch_json = nullptr;
  cJSON_ArrayForEach(patch_json, memory) {
    if (!cJSON_IsObject(patch_json)) {
      continue;
    }
    if (onion_cheat_entry_ensure_patch(entry) != 0) {
      return false;
    }
    if (parseMemoryObject(patch_json, &entry->patches[entry->patch_count])) {
      ++entry->patch_count;
    }
  }
  return true;
}

void parseAuthorArray(const cJSON *root, const char *key,
                      onion_cheat_file_t &out) {
  const cJSON *authors = onion_cjson::item(root, key);
  if (!cJSON_IsArray(authors)) {
    return;
  }
  cJSON *author = nullptr;
  cJSON_ArrayForEach(author, authors) {
    const char *name = onion_cjson::string_value(author);
    if (name) {
      onion_cheat_file_add_author(&out, name);
    }
  }
}

void clearEntry(onion_cheat_entry_t &entry) {
  if (entry.patches) {
    onion_cheat_secure_zero(entry.patches,
                            entry.patch_capacity * sizeof(onion_patch_t));
  }
  std::free(entry.patches);
  std::memset(&entry, 0, sizeof(entry));
}

} // namespace

class JsonCheatParser final : public ICheatParser {
public:
  const char *name() const override { return "json"; }

  int parse(const uint8_t *data, size_t size, onion_cheat_file_t &out) override {
    if (!data || size == 0) {
      return -1;
    }
    onion_cjson::Root root(reinterpret_cast<const char *>(data), size);
    if (!root || !cJSON_IsObject(root.get())) {
      return -1;
    }

    onion_cheat_file_clear(&out);
    if (!copyStringItem(root.get(), "process", out.process,
                        sizeof(out.process)) ||
        !copyStringItem(root.get(), "name", out.name, sizeof(out.name))) {
      return -1;
    }

    parseAuthorArray(root.get(), "credits", out);
    parseAuthorArray(root.get(), "authors", out);

    const cJSON *mods = onion_cjson::item(root.get(), "mods");
    if (!cJSON_IsArray(mods)) {
      return -1;
    }
    cJSON *mod = nullptr;
    cJSON_ArrayForEach(mod, mods) {
      if (!cJSON_IsObject(mod)) {
        continue;
      }
      onion_cheat_entry_t entry{};
      if (!parseModObject(mod, out.process, &entry) || entry.patch_count == 0) {
        clearEntry(entry);
        continue;
      }
      if (onion_cheat_file_ensure_cheat(&out) != 0) {
        clearEntry(entry);
        break;
      }
      out.cheats[out.cheat_count] = entry;
      ++out.cheat_count;
    }
    return out.cheat_count > 0 ? 0 : -1;
  }
};

std::unique_ptr<ICheatParser> makeJsonCheatParser() {
  return std::make_unique<JsonCheatParser>();
}

} // namespace onion::cheats
