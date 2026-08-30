#include <onion/log.h>
#include "cheats/i_cheat_parser.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "cheats/cheat_engine_internal.h"


namespace onion::cheats {
namespace {

const char *findXmlTagValue(const char *start, const char *tag, char *out,
                            size_t out_size) {
  char open_tag[64];
  char close_tag[64];

  std::snprintf(open_tag, sizeof(open_tag), "<%s>", tag);
  std::snprintf(close_tag, sizeof(close_tag), "</%s>", tag);

  const char *open = std::strstr(start, open_tag);
  if (open == nullptr) {
    out[0] = '\0';
    return nullptr;
  }
  open += std::strlen(open_tag);
  const char *close = std::strstr(open, close_tag);
  if (close == nullptr) {
    out[0] = '\0';
    return nullptr;
  }

  size_t len = static_cast<size_t>(close - open);
  if (len >= out_size) {
    len = out_size - 1;
  }
  std::memcpy(out, open, len);
  out[len] = '\0';
  onion_cheat_xml_unescape(out);
  return close + std::strlen(close_tag);
}

int findXmlAttr(const char *start, const char *tag, const char *attr, char *out,
                size_t out_size) {
  char marker[64];
  char attr_marker[64];

  std::snprintf(marker, sizeof(marker), "<%s", tag);
  std::snprintf(attr_marker, sizeof(attr_marker), "%s=\"", attr);
  const char *node = std::strstr(start, marker);
  if (node == nullptr) {
    out[0] = '\0';
    return -1;
  }
  const char *val = std::strstr(node, attr_marker);
  if (val == nullptr) {
    out[0] = '\0';
    return -1;
  }
  val += std::strlen(attr_marker);
  const char *end = std::strchr(val, '"');
  if (end == nullptr) {
    out[0] = '\0';
    return -1;
  }
  size_t len = static_cast<size_t>(end - val);
  if (len >= out_size) {
    len = out_size - 1;
  }
  std::memcpy(out, val, len);
  out[len] = '\0';
  onion_cheat_xml_unescape(out);
  return 0;
}

void stripDashes(char *s) {
  char *p = s;
  while (*p != '\0') {
    if (*p == '-') {
      std::memmove(p, p + 1, std::strlen(p));
    } else {
      ++p;
    }
  }
}

/** Mutating SHN/XML parse (entities replaced in-place; buffer must be writable). */
int parseXmlMutating(char *xml, onion_cheat_file_t &out) {
  const char *cursor = xml;
  char process[128];
  char game_name[128];

  LOG_DEBUG("[engine] parse_xml begin");
  onion_cheat_file_clear(&out);
  onion_cheat_replace_all(xml, 65536, "&lt;", "<");
  onion_cheat_replace_all(xml, 65536, "&gt;", ">");
  onion_cheat_replace_all(xml, 65536, "\\&quot;", "\"");
  onion_cheat_replace_all(xml, 65536, "&quot;", "\"");

  char moder[ONION_AUTHOR_NAME_LEN];

  if (findXmlAttr(xml, "Trainer", "Process", process, sizeof(process)) < 0 ||
      findXmlAttr(xml, "Trainer", "Game", game_name, sizeof(game_name)) < 0) {
    LOG_ERROR("[engine] parse_xml trainer attrs missing");
    return -1;
  }
  std::snprintf(out.process, sizeof(out.process), "%s", process);
  std::snprintf(out.name, sizeof(out.name), "%s", game_name);
  /* GoldHEN SHN/MC4: Moder="AuthorName" on Trainer */
  moder[0] = '\0';
  if (findXmlAttr(xml, "Trainer", "Moder", moder, sizeof(moder)) == 0 &&
      moder[0] != '\0') {
    onion_cheat_file_add_author(&out, moder);
  }
  LOG_DEBUG("[engine] parse_xml trainer process=%s game=%s", out.process,
            out.name);

  while ((cursor = std::strstr(cursor, "<Cheat ")) != nullptr) {
    const char *cheat_end = std::strstr(cursor, "</Cheat>");
    const char *line_cursor = cursor;
    char name[128];
    char description[256];

    if (onion_cheat_file_ensure_cheat(&out) != 0) {
      break;
    }
    onion_cheat_entry_t *entry = &out.cheats[out.cheat_count];

    if (cheat_end == nullptr) {
      LOG_ERROR("[engine] parse_xml cheat_end missing");
      break;
    }
    std::memset(entry, 0, sizeof(*entry));
    if (findXmlAttr(cursor, "Cheat", "Text", name, sizeof(name)) < 0) {
      LOG_ERROR("[engine] parse_xml cheat name missing");
      cursor = cheat_end + 8;
      continue;
    }
    description[0] = '\0';
    (void)findXmlAttr(cursor, "Cheat", "Description", description,
                      sizeof(description));
    std::snprintf(entry->name, sizeof(entry->name), "%s", name);
    std::snprintf(entry->description, sizeof(entry->description), "%s",
                  description);
    std::snprintf(entry->module_name, sizeof(entry->module_name), "%s",
                  process);

    while ((line_cursor = std::strstr(line_cursor, "<Cheatline>")) != nullptr &&
           line_cursor < cheat_end) {
      const char *line_end = std::strstr(line_cursor, "</Cheatline>");
      char offset[64];
      char section[32];
      char on[512];
      char off[512];
      char absolute[32];
      onion_patch_t *patch = nullptr;

      if (line_end == nullptr || line_end > cheat_end) {
        LOG_ERROR("[engine] parse_xml line_end missing cheat=%s",
                     entry->name);
        break;
      }

      offset[0] = '\0';
      section[0] = '\0';
      on[0] = '\0';
      off[0] = '\0';
      absolute[0] = '\0';
      findXmlTagValue(line_cursor, "Offset", offset, sizeof(offset));
      findXmlTagValue(line_cursor, "Section", section, sizeof(section));
      findXmlTagValue(line_cursor, "ValueOn", on, sizeof(on));
      findXmlTagValue(line_cursor, "ValueOff", off, sizeof(off));
      findXmlTagValue(line_cursor, "Absolute", absolute, sizeof(absolute));

      if (offset[0] == '\0' || on[0] == '\0' || off[0] == '\0') {
        LOG_WARN("[engine] parse_xml incomplete patch cheat=%s", entry->name);
        line_cursor = line_end + 12;
        continue;
      }

      if (onion_cheat_entry_ensure_patch(entry) != 0) {
        LOG_ERROR("[engine] parse_xml ensure_patch failed");
        line_cursor = line_end + 12;
        continue;
      }
      patch = &entry->patches[entry->patch_count];
      std::memset(patch, 0, sizeof(*patch));
      patch->offset = std::strtoull(offset, nullptr, 16);
      if (section[0] != '\0') {
        const int section_num = std::atoi(section);
        if (section_num < MODULE_INFO_MAX_SECTIONS) {
          patch->section = section_num;
        }
      }
      patch->absolute = absolute[0] != '\0';

      stripDashes(on);
      stripDashes(off);

      if (onion_cheat_hex_decode(on, patch->on, sizeof(patch->on),
                                 &patch->on_len) == 0 &&
          onion_cheat_hex_decode(off, patch->off, sizeof(patch->off),
                                 &patch->off_len) == 0) {
        entry->patch_count++;
      }
      line_cursor = line_end + 12;
    }

    if (entry->patch_count > 0) {
      LOG_TRACE("[engine] parse_xml cheat=%s patches=%zu", entry->name,
                entry->patch_count);
      out.cheat_count++;
    }
    cursor = cheat_end + 8;
  }

  LOG_DEBUG("[engine] parse_xml done cheats=%zu", out.cheat_count);
  return out.cheat_count > 0 ? 0 : -1;
}

} // namespace

/** Internal: parse writable NUL-terminated XML (used by MC4 after decrypt). */
int parseXmlBuffer(char *xml, onion_cheat_file_t &out) {
  if (xml == nullptr) {
    return -1;
  }
  return parseXmlMutating(xml, out);
}

class XmlCheatParser final : public ICheatParser {
public:
  const char *name() const override { return "shn"; }

  int parse(const uint8_t *data, size_t size, onion_cheat_file_t &out) override {
    if (data == nullptr || size == 0) {
      return -1;
    }
    /* Writable copy; entity replace may shrink only. Cap for replace_all. */
    const size_t cap = size + 1 < 65536 ? 65536 : size + 1;
    std::vector<char> buf(cap, '\0');
    std::memcpy(buf.data(), data, size);
    buf[size] = '\0';
    return parseXmlMutating(buf.data(), out);
  }
};

std::unique_ptr<ICheatParser> makeXmlCheatParser() {
  return std::make_unique<XmlCheatParser>();
}

} // namespace onion::cheats
