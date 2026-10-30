#include <onion/log.h>
#include "cheats/i_cheat_parser.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

#include "cheats/cheat_engine_internal.h"
#include "cheats/runtime.h"


namespace onion::cheats {

/* Factories implemented in format translation units */
std::unique_ptr<ICheatParser> makeJsonCheatParser();
std::unique_ptr<ICheatParser> makeXmlCheatParser();
std::unique_ptr<ICheatParser> makePsGamePatchXmlParser();
std::unique_ptr<ICheatParser> makeMc4CheatParser();
std::unique_ptr<ICheatParser> makeShnExtCheatParser();

namespace {

std::string normalizeFormat(std::string format) {
  while (!format.empty() && format.front() == '.') {
    format.erase(format.begin());
  }
  for (char &c : format) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return format;
}

} // namespace

std::unique_ptr<ICheatParser>
CheatParserFactory::createByFormat(const std::string &format) {
  const std::string f = normalizeFormat(format);
  if (f == "shn") {
    return makeXmlCheatParser();
  }
  if (f == "xml") {
    return makePsGamePatchXmlParser();
  }
  if (f == "mc4") {
    return makeMc4CheatParser();
  }
  if (f == "shnext") {
    return makeShnExtCheatParser();
  }
  /* json or unknown → JSON (GoldHEN-style default) */
  return makeJsonCheatParser();
}

std::unique_ptr<ICheatParser>
CheatParserFactory::createByPath(const std::string &path) {
  char format[16];
  if (!onion_cheat_match_ext(path.c_str(), format, sizeof(format))) {
    return createByFormat({});
  }
  return createByFormat(format);
}

int CheatParserFactory::loadBuffer(const std::string &format,
                                   const uint8_t *data, size_t size,
                                   onion_cheat_file_t &out) {
  if (data == nullptr || size == 0) {
    return -1;
  }
  auto parser = createByFormat(format);
  LOG_DEBUG("[engine] parse format=%s size=%zu", parser->name(), size);
  return parser->parse(data, size, out);
}

int CheatParserFactory::loadFile(const std::string &path,
                                 onion_cheat_file_t &out) {
  long size = 0;
  char *buf = nullptr;

  onion_cheat_file_clear(&out);
  if (path.empty()) {
    return -1;
  }

  LOG_DEBUG("[engine] load path=%s", path.c_str());
  buf = onion_cheat_load_file_buffer(path.c_str(), &size);
  if (buf == nullptr || size <= 0) {
    LOG_ERROR("[engine] load failed path=%s", path.c_str());
    free(buf);
    return -1;
  }

  auto parser = createByPath(path);
  if (std::strcmp(parser->name(), "xml") == 0) {
    const size_t slash = path.find_last_of("/\\");
    const std::string filename =
        slash == std::string::npos ? path : path.substr(slash + 1);
    onion_cheat_filename_t parts{};
    if (onion_cheat_parse_filename(filename.c_str(), &parts) == 0) {
      parser->setTargetGame(parts.title_id, parts.version);
    }
  }
  const int rc = parser->parse(reinterpret_cast<const uint8_t *>(buf),
                               static_cast<size_t>(size), out);

  onion_cheat_secure_zero(buf, static_cast<size_t>(size));
  free(buf);
  LOG_DEBUG("[engine] loaded format=%s cheats=%zu rc=%d", parser->name(),
            out.cheat_count, rc);
  return rc;
}

} // namespace onion::cheats
