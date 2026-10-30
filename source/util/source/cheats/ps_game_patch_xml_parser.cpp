#include <onion/log.h>
#include "cheats/i_cheat_parser.hpp"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cheats/cheat_engine_internal.h"

namespace onion::cheats {
namespace {

struct XmlAttribute {
  std::string name;
  std::string value;
};

struct XmlNode {
  std::string name;
  std::string text;
  std::vector<XmlAttribute> attributes;
  std::vector<size_t> children;
};

bool isSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string_view trim(std::string_view value) {
  while (!value.empty() && isSpace(value.front())) {
    value.remove_prefix(1);
  }
  while (!value.empty() && isSpace(value.back())) {
    value.remove_suffix(1);
  }
  return value;
}

bool appendUtf8(uint32_t cp, std::string &out) {
  if (cp == 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
    return false;
  }
  if (cp <= 0x7f) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7ff) {
    out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  } else if (cp <= 0xffff) {
    out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  } else {
    out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  }
  return true;
}

bool parseCodepoint(std::string_view digits, unsigned base, uint32_t &out) {
  if (digits.empty()) {
    return false;
  }
  uint64_t value = 0;
  for (char c : digits) {
    unsigned digit;
    if (c >= '0' && c <= '9') {
      digit = static_cast<unsigned>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      digit = static_cast<unsigned>(c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
      digit = static_cast<unsigned>(c - 'A' + 10);
    } else {
      return false;
    }
    if (digit >= base || value > (0x10ffffu - digit) / base) {
      return false;
    }
    value = value * base + digit;
  }
  out = static_cast<uint32_t>(value);
  return true;
}

bool decodeEntities(std::string_view input, std::string &out) {
  for (size_t i = 0; i < input.size();) {
    if (input[i] != '&') {
      if (input[i] == '\0') {
        return false;
      }
      out.push_back(input[i++]);
      continue;
    }
    const size_t end = input.find(';', i + 1);
    if (end == std::string_view::npos) {
      return false;
    }
    const std::string_view entity = input.substr(i + 1, end - i - 1);
    if (entity == "amp") {
      out.push_back('&');
    } else if (entity == "lt") {
      out.push_back('<');
    } else if (entity == "gt") {
      out.push_back('>');
    } else if (entity == "quot") {
      out.push_back('"');
    } else if (entity == "apos") {
      out.push_back('\'');
    } else if (!entity.empty() && entity.front() == '#') {
      const bool hex = entity.size() > 1 &&
                       (entity[1] == 'x' || entity[1] == 'X');
      const std::string_view digits = entity.substr(hex ? 2 : 1);
      uint32_t cp = 0;
      if (!parseCodepoint(digits, hex ? 16u : 10u, cp) ||
          !appendUtf8(cp, out)) {
        return false;
      }
    } else {
      return false;
    }
    i = end + 1;
  }
  return true;
}

bool isNameStart(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
         c == ':';
}

bool isNameChar(char c) {
  return isNameStart(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

class XmlDocumentParser {
public:
  bool parse(const uint8_t *data, size_t size) {
    if (data == nullptr || size == 0 || size > 4 * 1024 * 1024) {
      return false;
    }
    input_ = std::string_view(reinterpret_cast<const char *>(data), size);
    if (input_.size() >= 3 &&
        static_cast<uint8_t>(input_[0]) == 0xef &&
        static_cast<uint8_t>(input_[1]) == 0xbb &&
        static_cast<uint8_t>(input_[2]) == 0xbf) {
      pos_ = 3;
    }
    XmlNode document_node;
    document_node.name = "#document";
    nodes_.push_back(std::move(document_node));
    stack_.push_back(0);
    size_t root_count = 0;

    while (pos_ < input_.size()) {
      if (input_[pos_] != '<') {
        if (!parseText()) {
          return false;
        }
        continue;
      }
      if (startsWith("<!--")) {
        if (!parseComment()) {
          return false;
        }
      } else if (startsWith("<?")) {
        if (!parseProcessingInstruction()) {
          return false;
        }
      } else if (startsWith("</")) {
        if (!parseEndTag()) {
          return false;
        }
      } else if (startsWith("<!")) {
        return false; // DTD and other declarations are not needed here.
      } else {
        if (!parseStartTag(root_count)) {
          return false;
        }
      }
    }
    return root_count == 1 && stack_.size() == 1 &&
           nodes_[0].children.size() == 1;
  }

  const std::vector<XmlNode> &nodes() const { return nodes_; }

private:
  bool startsWith(std::string_view value) const {
    return input_.substr(pos_, value.size()) == value;
  }

  void skipSpace() {
    while (pos_ < input_.size() && isSpace(input_[pos_])) {
      ++pos_;
    }
  }

  bool parseName(std::string &out) {
    if (pos_ >= input_.size() || !isNameStart(input_[pos_])) {
      return false;
    }
    const size_t begin = pos_++;
    while (pos_ < input_.size() && isNameChar(input_[pos_])) {
      ++pos_;
    }
    out.assign(input_.substr(begin, pos_ - begin));
    return true;
  }

  bool parseText() {
    const size_t end = input_.find('<', pos_);
    const size_t stop = end == std::string_view::npos ? input_.size() : end;
    std::string decoded;
    if (!decodeEntities(input_.substr(pos_, stop - pos_), decoded)) {
      return false;
    }
    if (stack_.size() == 1) {
      if (!trim(decoded).empty()) {
        return false;
      }
    } else {
      nodes_[stack_.back()].text += decoded;
    }
    pos_ = stop;
    return true;
  }

  bool parseComment() {
    const size_t end = input_.find("-->", pos_ + 4);
    if (end == std::string_view::npos ||
        input_.substr(pos_ + 4, end - pos_ - 4).find("--") !=
            std::string_view::npos) {
      return false;
    }
    pos_ = end + 3;
    return true;
  }

  bool parseProcessingInstruction() {
    const size_t end = input_.find("?>", pos_ + 2);
    if (end == std::string_view::npos) {
      return false;
    }
    pos_ = end + 2;
    return true;
  }

  bool parseEndTag() {
    pos_ += 2;
    std::string name;
    if (!parseName(name)) {
      return false;
    }
    skipSpace();
    if (pos_ >= input_.size() || input_[pos_++] != '>' || stack_.size() <= 1 ||
        nodes_[stack_.back()].name != name) {
      return false;
    }
    stack_.pop_back();
    return true;
  }

  bool parseStartTag(size_t &root_count) {
    ++pos_; // '<'
    XmlNode node;
    if (!parseName(node.name)) {
      return false;
    }
    for (;;) {
      const size_t before_space = pos_;
      skipSpace();
      const bool separated = pos_ != before_space;
      if (pos_ >= input_.size()) {
        return false;
      }
      if (input_[pos_] == '>') {
        ++pos_;
        break;
      }
      if (input_[pos_] == '/' && pos_ + 1 < input_.size() &&
          input_[pos_ + 1] == '>') {
        pos_ += 2;
        break;
      }
      if (!separated) {
        return false;
      }

      XmlAttribute attr;
      if (node.attributes.size() >= 64) {
        return false;
      }
      if (!parseName(attr.name)) {
        return false;
      }
      for (const XmlAttribute &previous : node.attributes) {
        if (previous.name == attr.name) {
          return false;
        }
      }
      skipSpace();
      if (pos_ >= input_.size() || input_[pos_++] != '=') {
        return false;
      }
      skipSpace();
      if (pos_ >= input_.size() ||
          (input_[pos_] != '\'' && input_[pos_] != '"')) {
        return false;
      }
      const char quote = input_[pos_++];
      const size_t end = input_.find(quote, pos_);
      if (end == std::string_view::npos ||
          input_.substr(pos_, end - pos_).find('<') != std::string_view::npos ||
          !decodeEntities(input_.substr(pos_, end - pos_), attr.value)) {
        return false;
      }
      pos_ = end + 1;
      node.attributes.push_back(std::move(attr));
    }

    if (nodes_.size() >= 200000) {
      return false;
    }
    const bool self_closing = input_[pos_ - 2] == '/' &&
                              input_[pos_ - 1] == '>';
    const size_t parent = stack_.back();
    if (parent == 0 && ++root_count > 1) {
      return false;
    }
    const size_t index = nodes_.size();
    nodes_.push_back(std::move(node));
    nodes_[parent].children.push_back(index);
    if (!self_closing) {
      stack_.push_back(index);
    }
    return true;
  }

  std::string_view input_;
  size_t pos_ = 0;
  std::vector<XmlNode> nodes_;
  std::vector<size_t> stack_;
};

std::vector<const XmlNode *> childrenNamed(const std::vector<XmlNode> &nodes,
                                           const XmlNode &parent,
                                           const char *name) {
  std::vector<const XmlNode *> result;
  for (size_t index : parent.children) {
    if (nodes[index].name == name) {
      result.push_back(&nodes[index]);
    }
  }
  return result;
}

const std::string *attribute(const XmlNode &node, const char *name) {
  for (const XmlAttribute &attr : node.attributes) {
    if (attr.name == name) {
      return &attr.value;
    }
  }
  return nullptr;
}

bool uniqueNodeValue(const std::vector<XmlNode> &nodes, const XmlNode &node,
                     const char *name, std::string_view &value) {
  const std::string *attr = attribute(node, name);
  const XmlNode *child = nullptr;
  size_t child_count = 0;
  for (size_t index : node.children) {
    if (nodes[index].name == name) {
      child = &nodes[index];
      ++child_count;
    }
  }
  if (child_count > 1 || (attr != nullptr && child_count != 0)) {
    return false;
  }
  if (attr != nullptr) {
    value = trim(*attr);
  } else if (child != nullptr) {
    value = trim(child->text);
  } else {
    value = {};
  }
  return true;
}

std::string_view nodeValue(const std::vector<XmlNode> &nodes,
                           const XmlNode &node, const char *name) {
  std::string_view value;
  return uniqueNodeValue(nodes, node, name, value) ? value
                                                   : std::string_view{};
}

bool copyString(std::string_view value, char *out, size_t capacity,
                bool required) {
  value = trim(value);
  if (capacity == 0 || (required && value.empty()) || value.size() >= capacity) {
    return false;
  }
  if (!value.empty()) {
    std::memcpy(out, value.data(), value.size());
  }
  out[value.size()] = '\0';
  return true;
}

bool versionMatches(std::string_view versions, std::string_view target) {
  while (!versions.empty()) {
    const size_t comma = versions.find(',');
    const std::string_view candidate =
        trim(versions.substr(0, comma));
    const bool mask_version =
        candidate.size() == 4 &&
        (candidate[0] == 'm' || candidate[0] == 'M') &&
        (candidate[1] == 'a' || candidate[1] == 'A') &&
        (candidate[2] == 's' || candidate[2] == 'S') &&
        (candidate[3] == 'k' || candidate[3] == 'K');
    if (candidate == target || mask_version) {
      return true;
    }
    if (comma == std::string_view::npos) {
      break;
    }
    versions.remove_prefix(comma + 1);
  }
  return false;
}

std::string uppercaseAscii(std::string_view value) {
  std::string result(value);
  for (char &c : result) {
    if (c >= 'a' && c <= 'z') {
      c = static_cast<char>(c - 'a' + 'A');
    }
  }
  return result;
}

bool parseUnsignedDigits(std::string_view value, unsigned base,
                         uint64_t &out) {
  if (value.empty()) {
    return false;
  }
  uint64_t result = 0;
  for (char c : value) {
    unsigned digit;
    if (c >= '0' && c <= '9') {
      digit = static_cast<unsigned>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      digit = static_cast<unsigned>(c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
      digit = static_cast<unsigned>(c - 'A' + 10);
    } else {
      return false;
    }
    if (digit >= base ||
        result > (std::numeric_limits<uint64_t>::max() - digit) / base) {
      return false;
    }
    result = result * base + digit;
  }
  out = result;
  return true;
}

bool parseHex(std::string_view value, uint64_t &out) {
  value = trim(value);
  if (value.size() >= 2 && value[0] == '0' &&
      (value[1] == 'x' || value[1] == 'X')) {
    value.remove_prefix(2);
  } else if (!value.empty() && (value.front() == '#' || value.front() == '$')) {
    value.remove_prefix(1);
  }
  return parseUnsignedDigits(value, 16, out);
}

bool parseAddressNumber(std::string_view value, uint64_t &out) {
  value = trim(value);
  if (value.size() >= 2 && value[0] == '0' &&
      (value[1] == 'x' || value[1] == 'X')) {
    value.remove_prefix(2);
    return parseUnsignedDigits(value, 16, out);
  }
  if (value.size() > 1 && value.front() == '0') {
    return parseUnsignedDigits(value.substr(1), 8, out);
  }
  return parseUnsignedDigits(value, 10, out);
}

bool parseSignedOffset(std::string_view value, int64_t &out) {
  value = trim(value);
  if (value.empty()) {
    out = 0;
    return true;
  }
  bool negative = false;
  if (value.front() == '-' || value.front() == '+') {
    negative = value.front() == '-';
    value.remove_prefix(1);
  }
  unsigned base = 10;
  if (value.size() >= 2 && value[0] == '0' &&
      (value[1] == 'x' || value[1] == 'X')) {
    value.remove_prefix(2);
    base = 16;
  } else if (!value.empty() && (value.front() == '#' || value.front() == '$')) {
    value.remove_prefix(1);
    base = 16;
  } else if (value.size() > 1 && value.front() == '0') {
    value.remove_prefix(1);
    base = 8;
  }
  uint64_t magnitude = 0;
  const uint64_t negative_limit =
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1;
  if (!parseUnsignedDigits(value, base, magnitude) ||
      magnitude > (negative ? negative_limit
                            : static_cast<uint64_t>(
                                  std::numeric_limits<int64_t>::max()))) {
    return false;
  }
  if (negative && magnitude == negative_limit) {
    out = std::numeric_limits<int64_t>::min();
  } else {
    out = negative ? -static_cast<int64_t>(magnitude)
                   : static_cast<int64_t>(magnitude);
  }
  return true;
}

int hexNibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

bool decodePattern(std::string_view value, onion_patch_t &patch) {
  size_t fixed_bytes = 0;
  for (size_t i = 0; i < value.size();) {
    if (isSpace(value[i])) {
      ++i;
      continue;
    }
    if (patch.pattern_len >= ONION_MAX_PATTERN_BYTES) {
      return false;
    }
    if (value[i] == '?') {
      ++i;
      if (i < value.size() && value[i] == '?') {
        ++i;
      }
      patch.pattern[patch.pattern_len] = 0;
      patch.pattern_mask[patch.pattern_len++] = 0;
      continue;
    }
    if (i + 1 >= value.size()) {
      return false;
    }
    const int high = hexNibble(value[i]);
    const int low = hexNibble(value[i + 1]);
    if (high < 0 || low < 0) {
      return false;
    }
    patch.pattern[patch.pattern_len] =
        static_cast<uint8_t>((high << 4) | low);
    patch.pattern_mask[patch.pattern_len++] = 0xff;
    ++fixed_bytes;
    i += 2;
  }
  return patch.pattern_len > 0 && fixed_bytes > 0;
}

bool parseJumpSize(std::string_view value, uint16_t &out) {
  value = trim(value);
  if (value.empty()) {
    out = 5;
    return true;
  }
  uint64_t parsed = 0;
  if (!parseUnsignedDigits(value, 10, parsed) || parsed < 5 ||
      parsed > ONION_MAX_PATCH_BYTES) {
    return false;
  }
  out = static_cast<uint16_t>(parsed);
  return true;
}

bool parseTypedInteger(std::string_view value, size_t width, uint64_t &out) {
  value = trim(value);
  bool negative = false;
  if (!value.empty() && value.front() == '-') {
    negative = true;
    value.remove_prefix(1);
  }
  unsigned base = 10;
  if (value.size() >= 2 && value[0] == '0' &&
      (value[1] == 'x' || value[1] == 'X')) {
    value.remove_prefix(2);
    base = 16;
  } else if (!value.empty() &&
             (value.front() == '#' || value.front() == '$')) {
    value.remove_prefix(1);
    base = 16;
  }
  uint64_t number = 0;
  if (!parseUnsignedDigits(value, base, number)) {
    return false;
  }
  if (negative) {
    const unsigned bits = static_cast<unsigned>(width * 8);
    const uint64_t max_magnitude = uint64_t{1} << (bits - 1);
    if (number > max_magnitude) {
      return false;
    }
    number = uint64_t{0} - number;
  } else if (width < sizeof(number) && (number >> (width * 8)) != 0) {
    return false;
  }
  out = number;
  return true;
}

bool decodeBytes(std::string_view value, uint8_t *out, size_t capacity,
                 size_t &length) {
  value = trim(value);
  if (value.size() >= 2 && value[0] == '0' &&
      (value[1] == 'x' || value[1] == 'X')) {
    value.remove_prefix(2);
  } else if (!value.empty() && (value.front() == '#' || value.front() == '$')) {
    value.remove_prefix(1);
  }
  std::string digits;
  digits.reserve(value.size());
  for (char c : value) {
    if (isSpace(c) || c == '-' || c == '_') {
      continue;
    }
    digits.push_back(c);
  }
  if (digits.empty() || (digits.size() & 1) != 0 ||
      digits.size() / 2 > capacity || digits.size() / 2 > 255) {
    return false;
  }
  for (size_t i = 0; i < digits.size(); i += 2) {
    uint64_t byte = 0;
    if (!parseHex(std::string_view(digits).substr(i, 2), byte)) {
      return false;
    }
    out[i / 2] = static_cast<uint8_t>(byte);
  }
  length = digits.size() / 2;
  return true;
}

bool decodeUtf8(std::string_view input, std::vector<uint32_t> &codepoints) {
  for (size_t i = 0; i < input.size();) {
    const uint8_t first = static_cast<uint8_t>(input[i++]);
    uint32_t cp;
    size_t following;
    if (first <= 0x7f) {
      cp = first;
      following = 0;
    } else if (first >= 0xc2 && first <= 0xdf) {
      cp = first & 0x1f;
      following = 1;
    } else if (first >= 0xe0 && first <= 0xef) {
      cp = first & 0x0f;
      following = 2;
    } else if (first >= 0xf0 && first <= 0xf4) {
      cp = first & 0x07;
      following = 3;
    } else {
      return false;
    }
    if (following > input.size() - i) {
      return false;
    }
    for (size_t n = 0; n < following; ++n) {
      const uint8_t next = static_cast<uint8_t>(input[i++]);
      if ((next & 0xc0) != 0x80) {
        return false;
      }
      cp = (cp << 6) | (next & 0x3f);
    }
    if ((following == 1 && cp < 0x80) ||
        (following == 2 && cp < 0x800) ||
        (following == 3 && cp < 0x10000) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff) || cp == 0) {
      return false;
    }
    codepoints.push_back(cp);
  }
  return true;
}

bool parseStringEscape(std::string_view input, size_t &position,
                       uint32_t &codepoint) {
  if (position >= input.size() || input[position] != '\\' ||
      position + 1 >= input.size()) {
    return false;
  }
  position += 1;
  const char escaped = input[position++];
  switch (escaped) {
  case 'n':
    codepoint = '\n';
    return true;
  case 't':
    codepoint = '\t';
    return true;
  case 'r':
    codepoint = '\r';
    return true;
  case '0':
    codepoint = 0;
    return true;
  case '\\':
    codepoint = '\\';
    return true;
  case 'x': {
    if (input.size() - position < 2) {
      return false;
    }
    uint64_t byte = 0;
    if (!parseUnsignedDigits(input.substr(position, 2), 16, byte)) {
      return false;
    }
    position += 2;
    codepoint = static_cast<uint32_t>(byte);
    return true;
  }
  default:
    return false;
  }
}

bool appendUtf8Value(std::string_view value, onion_patch_t &patch) {
  for (size_t position = 0; position < value.size();) {
    if (value[position] == '\\') {
      uint32_t cp = 0;
      if (!parseStringEscape(value, position, cp) || cp > 0xff ||
          patch.on_len >= sizeof(patch.on)) {
        return false;
      }
      patch.on[patch.on_len++] = static_cast<uint8_t>(cp);
      continue;
    }
    const size_t escape = value.find('\\', position);
    const size_t end = escape == std::string_view::npos ? value.size() : escape;
    std::vector<uint32_t> codepoints;
    const std::string_view segment = value.substr(position, end - position);
    if (!decodeUtf8(segment, codepoints) ||
        segment.size() > sizeof(patch.on) - patch.on_len) {
      return false;
    }
    std::memcpy(patch.on + patch.on_len, segment.data(), segment.size());
    patch.on_len += segment.size();
    position = end;
  }
  return patch.on_len > 0;
}

bool decodeStringCodepoints(std::string_view value,
                            std::vector<uint32_t> &codepoints) {
  for (size_t position = 0; position < value.size();) {
    if (value[position] == '\\') {
      uint32_t cp = 0;
      if (!parseStringEscape(value, position, cp)) {
        return false;
      }
      codepoints.push_back(cp);
      continue;
    }
    const size_t escape = value.find('\\', position);
    const size_t end = escape == std::string_view::npos ? value.size() : escape;
    std::vector<uint32_t> segment;
    if (!decodeUtf8(value.substr(position, end - position), segment)) {
      return false;
    }
    codepoints.insert(codepoints.end(), segment.begin(), segment.end());
    position = end;
  }
  return !codepoints.empty();
}

bool decodeValue(std::string_view type, std::string_view value,
                 onion_patch_t &patch) {
  if (type == "bytes") {
    return decodeBytes(value, patch.on, sizeof(patch.on), patch.on_len);
  }
  if (type == "utf8") {
    if (value.empty() || value.size() > sizeof(patch.on) ||
        !appendUtf8Value(value, patch)) {
      return false;
    }
    return true;
  }
  if (type == "utf16") {
    std::vector<uint32_t> codepoints;
    if (value.empty() || value.size() > sizeof(patch.on) ||
        !decodeStringCodepoints(value, codepoints)) {
      return false;
    }
    for (uint32_t cp : codepoints) {
      if (cp <= 0xffff) {
        if (patch.on_len + 2 > sizeof(patch.on)) {
          return false;
        }
        patch.on[patch.on_len++] = static_cast<uint8_t>(cp);
        patch.on[patch.on_len++] = static_cast<uint8_t>(cp >> 8);
      } else {
        if (patch.on_len + 4 > sizeof(patch.on)) {
          return false;
        }
        cp -= 0x10000;
        const uint16_t high = static_cast<uint16_t>(0xd800 | (cp >> 10));
        const uint16_t low = static_cast<uint16_t>(0xdc00 | (cp & 0x3ff));
        patch.on[patch.on_len++] = static_cast<uint8_t>(high);
        patch.on[patch.on_len++] = static_cast<uint8_t>(high >> 8);
        patch.on[patch.on_len++] = static_cast<uint8_t>(low);
        patch.on[patch.on_len++] = static_cast<uint8_t>(low >> 8);
      }
    }
    return true;
  }
  if (type == "float32") {
    std::string text(trim(value));
    if (text.empty()) {
      return false;
    }
    char *end = nullptr;
    errno = 0;
    const float number = std::strtof(text.c_str(), &end);
    if (errno == ERANGE || end == text.c_str() || *end != '\0' ||
        !std::isfinite(number)) {
      return false;
    }
    uint32_t bits;
    std::memcpy(&bits, &number, sizeof(bits));
    for (size_t i = 0; i < 4; ++i) {
      patch.on[i] = static_cast<uint8_t>(bits >> (i * 8));
    }
    patch.on_len = 4;
    return true;
  }
  if (type == "float64") {
    std::string text(trim(value));
    if (text.empty()) {
      return false;
    }
    char *end = nullptr;
    errno = 0;
    const double number = std::strtod(text.c_str(), &end);
    if (errno == ERANGE || end == text.c_str() || *end != '\0' ||
        !std::isfinite(number)) {
      return false;
    }
    uint64_t bits;
    std::memcpy(&bits, &number, sizeof(bits));
    for (size_t i = 0; i < 8; ++i) {
      patch.on[i] = static_cast<uint8_t>(bits >> (i * 8));
    }
    patch.on_len = 8;
    return true;
  }

  size_t width = 0;
  if (type == "byte") {
    width = 1;
  } else if (type == "bytes16") {
    width = 2;
  } else if (type == "bytes32") {
    width = 4;
  } else if (type == "bytes64") {
    width = 8;
  } else {
    return false;
  }
  uint64_t number = 0;
  if (!parseTypedInteger(value, width, number)) {
    return false;
  }
  for (size_t i = 0; i < width; ++i) {
    patch.on[i] = static_cast<uint8_t>(number >> (i * 8));
  }
  patch.on_len = width;
  return true;
}

bool buildMetadata(const std::vector<XmlNode> &nodes, const XmlNode &metadata,
                   uint64_t image_base, onion_cheat_entry_t &entry,
                   std::string &reason) {
  std::string_view name;
  std::string_view app_elf;
  std::string_view note;
  if (!uniqueNodeValue(nodes, metadata, "Name", name) ||
      !uniqueNodeValue(nodes, metadata, "AppElf", app_elf) ||
      !uniqueNodeValue(nodes, metadata, "Note", note)) {
    reason = "repeated or ambiguous Name/AppElf/Note";
    return false;
  }
  if (!copyString(name, entry.name, sizeof(entry.name), true) ||
      !copyString(app_elf, entry.module_name, sizeof(entry.module_name), true) ||
      !copyString(note, entry.description, sizeof(entry.description), false)) {
    reason = "missing or oversized Name/AppElf/Note";
    return false;
  }
  const std::vector<const XmlNode *> patch_lists =
      childrenNamed(nodes, metadata, "PatchList");
  if (patch_lists.size() != 1) {
    reason = "PatchList is missing or repeated";
    return false;
  }
  const XmlNode *list = patch_lists.front();
  const std::vector<const XmlNode *> lines = childrenNamed(nodes, *list, "Line");
  if (lines.empty()) {
    reason = "PatchList is empty";
    return false;
  }
  if (lines.size() > 4096) {
    reason = "PatchList exceeds the supported line count";
    return false;
  }
  for (size_t child : list->children) {
    if (nodes[child].name != "Line") {
      reason = "PatchList contains an unsupported element";
      return false;
    }
  }
  std::vector<onion_patch_t> patches;
  patches.reserve(lines.size());
  for (const XmlNode *line : lines) {
    onion_patch_t patch{};
    std::string_view type;
    std::string_view address;
    std::string_view value;
    std::string_view offset_text;
    std::string_view target_text;
    std::string_view size_text;
    if (!uniqueNodeValue(nodes, *line, "Type", type) ||
        !uniqueNodeValue(nodes, *line, "Address", address) ||
        !uniqueNodeValue(nodes, *line, "Value", value) ||
        !uniqueNodeValue(nodes, *line, "Offset", offset_text) ||
        !uniqueNodeValue(nodes, *line, "Target", target_text) ||
        !uniqueNodeValue(nodes, *line, "Size", size_text)) {
      reason = "line has repeated or ambiguous Type/Address/Value";
      return false;
    }

    bool is_direct = false;
    if (type == "mask" || type == "mask_jump32") {
      patch.kind = type == "mask" ? ONION_PATCH_MASK
                                  : ONION_PATCH_MASK_JUMP32;
      if (address.empty() || value.empty() ||
          !decodePattern(address, patch) ||
          !parseSignedOffset(offset_text, patch.pattern_offset) ||
          !decodeValue("bytes", value, patch)) {
        reason = "malformed pattern, offset, or payload";
        return false;
      }
      if (type == "mask_jump32") {
        size_t target_len = 0;
        if (!decodeBytes(target_text, patch.target, sizeof(patch.target),
                         target_len) ||
            target_len > ONION_MAX_PATTERN_BYTES ||
            !parseJumpSize(size_text, patch.jump_size)) {
          reason = "mask_jump32 requires valid Target bytes and Size >= 5";
          return false;
        }
        patch.target_len = static_cast<uint16_t>(target_len);
      } else if (!target_text.empty() || !size_text.empty()) {
        reason = "mask does not accept Target or Size";
        return false;
      }
    } else {
      patch.kind = ONION_PATCH_DIRECT;
      uint64_t parsed_address = 0;
      if (type.empty() || address.empty() || value.empty() ||
          !parseAddressNumber(address, parsed_address) ||
          parsed_address < image_base ||
          !decodeValue(type, value, patch)) {
        reason = "malformed address/value or unsupported Type";
        return false;
      }
      patch.offset = parsed_address - image_base;
      if (patch.offset > std::numeric_limits<uint64_t>::max() - patch.on_len) {
        reason = "patch address range overflows";
        return false;
      }
      is_direct = true;
    }

    if (is_direct) {
      for (const onion_patch_t &previous : patches) {
        if (previous.kind != ONION_PATCH_DIRECT) {
          continue;
        }
        const uint64_t previous_end = previous.offset + previous.on_len;
        const uint64_t current_end = patch.offset + patch.on_len;
        if (patch.offset < previous_end && previous.offset < current_end) {
          reason = "PatchList contains overlapping direct-address lines";
          return false;
        }
      }
    }
    patch.restore_original = true;
    patches.push_back(patch);
  }
  entry.patch_count = patches.size();
  entry.patch_capacity = patches.size();
  entry.patches = static_cast<onion_patch_t *>(
      std::calloc(patches.size(), sizeof(onion_patch_t)));
  if (entry.patches == nullptr) {
    entry.patch_count = 0;
    entry.patch_capacity = 0;
    reason = "out of memory";
    return false;
  }
  std::memcpy(entry.patches, patches.data(), patches.size() * sizeof(patches[0]));
  return true;
}

} // namespace

class PsGamePatchXmlParser final : public ICheatParser {
public:
  const char *name() const override { return "xml"; }

  void setTargetGame(const char *title_id, const char *version) override {
    target_title_id_ = title_id ? title_id : "";
    target_version_ = version ? version : "";
  }

  int parse(const uint8_t *data, size_t size, onion_cheat_file_t &out) override {
    onion_cheat_file_clear(&out);
    if (target_title_id_.empty() || target_version_.empty()) {
      LOG_WARN("[cheats] Illusion XML requires a TITLE_ID_VERSION filename");
      return -1;
    }
    XmlDocumentParser parser;
    if (!parser.parse(data, size)) {
      LOG_WARN("[cheats] malformed or unsupported Illusion XML document");
      return -1;
    }
    const std::vector<XmlNode> &nodes = parser.nodes();
    const XmlNode &root = nodes[nodes[0].children[0]];
    if (root.name != "Patch") {
      LOG_WARN("[cheats] Illusion XML root is not Patch");
      return -1;
    }
    const std::vector<const XmlNode *> title_lists =
        childrenNamed(nodes, root, "TitleID");
    if (title_lists.size() != 1) {
      LOG_WARN("[cheats] Illusion XML TitleID list is missing or repeated");
      return -1;
    }
    const XmlNode *title_list = title_lists.front();
    bool title_matches = false;
    for (const XmlNode *id : childrenNamed(nodes, *title_list, "ID")) {
      if (uppercaseAscii(trim(id->text)) == target_title_id_) {
        title_matches = true;
        break;
      }
    }
    if (!title_matches) {
      LOG_DEBUG("[cheats] Illusion XML title does not match %s",
                target_title_id_.c_str());
      return -1;
    }

    std::snprintf(out.name, sizeof(out.name), "Illusion PS-Game-Patch XML");
    const std::vector<const XmlNode *> metadata_nodes =
        childrenNamed(nodes, root, "Metadata");
    size_t accepted = 0;
    size_t total_patches = 0;
    for (const XmlNode *metadata : metadata_nodes) {
      std::string_view app_version;
      if (!uniqueNodeValue(nodes, *metadata, "AppVer", app_version)) {
        LOG_WARN("[cheats] skip Illusion metadata with ambiguous AppVer");
        continue;
      }
      if (!versionMatches(app_version, target_version_)) {
        continue;
      }
      uint64_t image_base = 0x00400000;
      std::string_view base;
      if (!uniqueNodeValue(nodes, *metadata, "ImageBase", base) ||
          (!base.empty() && !parseAddressNumber(base, image_base))) {
        LOG_WARN("[cheats] Illusion metadata has invalid or ambiguous ImageBase");
        continue;
      }
      onion_cheat_entry_t entry{};
      std::string reason;
      if (!buildMetadata(nodes, *metadata, image_base, entry, reason)) {
        std::string metadata_name(nodeValue(nodes, *metadata, "Name"));
        if (metadata_name.size() > 127) {
          metadata_name.resize(127);
        }
        LOG_WARN("[cheats] skip Illusion metadata '%s': %s",
                 metadata_name.c_str(), reason.c_str());
        if (entry.patches != nullptr) {
          onion_cheat_secure_zero(entry.patches,
                                  entry.patch_capacity * sizeof(onion_patch_t));
          std::free(entry.patches);
        }
        continue;
      }
      if (entry.patch_count > 4096 - total_patches) {
        onion_cheat_secure_zero(entry.patches,
                                entry.patch_capacity * sizeof(onion_patch_t));
        std::free(entry.patches);
        LOG_WARN("[cheats] Illusion XML exceeds total patch limit");
        onion_cheat_file_clear(&out);
        return -1;
      }
      if (out.process[0] == '\0') {
        std::snprintf(out.process, sizeof(out.process), "%s", entry.module_name);
      }
      if (onion_cheat_file_ensure_cheat(&out) != 0) {
        onion_cheat_secure_zero(entry.patches,
                                entry.patch_capacity * sizeof(onion_patch_t));
        std::free(entry.patches);
        onion_cheat_file_clear(&out);
        return -1;
      }
      out.cheats[out.cheat_count++] = entry;
      ++accepted;
      total_patches += entry.patch_count;
    }
    return accepted > 0 ? 0 : -1;
  }

private:
  std::string target_title_id_;
  std::string target_version_;
};

std::unique_ptr<ICheatParser> makePsGamePatchXmlParser() {
  return std::make_unique<PsGamePatchXmlParser>();
}

} // namespace onion::cheats
