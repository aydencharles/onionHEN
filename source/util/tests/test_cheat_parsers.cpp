#include <cstdint>
#include <cstring>
#include <string>

#include "cheats/i_cheat_parser.hpp"
#include "cheats/cheat_engine.h"
#include "test_harness.h"
#include "test_support.h"

using onion::cheats::CheatParserFactory;

namespace {

int load_path(const char *path, onion_cheat_file_t &file) {
  return CheatParserFactory::loadFile(path, file);
}

int load_buf(const char *format, const char *data, onion_cheat_file_t &file) {
  return CheatParserFactory::loadBuffer(
      format, reinterpret_cast<const uint8_t *>(data), std::strlen(data), file);
}

int test_parse_json_buffer_success() {
  const char *json =
      "{"
      "\"process\":\"eboot.bin\","
      "\"name\":\"Demo\","
      "\"mods\":[{"
      "\"name\":\"Infinite HP\","
      "\"description\":\"Lock HP\","
      "\"memory\":[{"
      "\"offset\":\"1000\","
      "\"on\":\"AABBCCDD\","
      "\"off\":\"11223344\","
      "\"section\":1"
      "}]"
      "}],"
      "\"credits\":[\"Alice\",\"Bob\"]"
      "}";
  static onion_cheat_file_t file;
  const uint8_t on_expected[] = {0xAA, 0xBB, 0xCC, 0xDD};
  const uint8_t off_expected[] = {0x11, 0x22, 0x33, 0x44};

  TEST_ASSERT_EQ_INT(0, load_buf("json", json, file));
  TEST_ASSERT_STREQ("eboot.bin", file.process);
  TEST_ASSERT_STREQ("Demo", file.name);
  TEST_ASSERT_EQ_INT(2, static_cast<int>(file.author_count));
  TEST_ASSERT_STREQ("Alice", file.authors[0]);
  TEST_ASSERT_STREQ("Bob", file.authors[1]);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(file.cheat_count));
  TEST_ASSERT_STREQ("Infinite HP", file.cheats[0].name);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(file.cheats[0].patch_count));
  TEST_ASSERT_EQ_INT(1, file.cheats[0].patches[0].section);
  TEST_ASSERT_EQ_U64(0x1000, file.cheats[0].patches[0].offset);
  TEST_ASSERT_MEMEQ(on_expected, file.cheats[0].patches[0].on,
                    sizeof(on_expected));
  TEST_ASSERT_MEMEQ(off_expected, file.cheats[0].patches[0].off,
                    sizeof(off_expected));
  onion_cheat_file_clear(&file);
  return 0;
}

int test_parse_json_authors_alias_and_dedup() {
  const char *json =
      "{"
      "\"process\":\"eboot.bin\","
      "\"name\":\"Demo\","
      "\"mods\":[{"
      "\"name\":\"Patch\","
      "\"memory\":[{"
      "\"offset\":\"10\","
      "\"on\":\"AA\","
      "\"off\":\"BB\""
      "}]"
      "}],"
      "\"credits\":[\"Same\"],"
      "\"authors\":[\"Same\",\"Extra\"]"
      "}";
  static onion_cheat_file_t file;

  TEST_ASSERT_EQ_INT(0, load_buf("json", json, file));
  TEST_ASSERT_EQ_INT(2, static_cast<int>(file.author_count));
  TEST_ASSERT_STREQ("Same", file.authors[0]);
  TEST_ASSERT_STREQ("Extra", file.authors[1]);
  onion_cheat_file_clear(&file);
  return 0;
}

int test_parse_json_decodes_escaped_strings() {
  const char *json =
      R"({"process":"eboot.bin","name":"Game \u014d","mods":[{"name":"Quote \" and slash \\","description":"Line\nBreak","memory":[{"offset":"10","on":"AA","off":"BB"}]}],"credits":["A\u006cice"]})";
  static onion_cheat_file_t file;

  TEST_ASSERT_EQ_INT(0, load_buf("json", json, file));
  TEST_ASSERT_STREQ("Game \u014d", file.name);
  TEST_ASSERT_STREQ("Alice", file.authors[0]);
  TEST_ASSERT_STREQ(R"(Quote " and slash \)", file.cheats[0].name);
  TEST_ASSERT_STREQ("Line\nBreak", file.cheats[0].description);
  onion_cheat_file_clear(&file);
  return 0;
}

int test_parse_json_rejects_trailing_garbage() {
  const char *json =
      R"({"process":"eboot.bin","name":"Demo","mods":[]} trailing)";
  static onion_cheat_file_t file;

  TEST_ASSERT_EQ_INT(-1, load_buf("json", json, file));
  onion_cheat_file_clear(&file);
  return 0;
}

int test_parse_json_buffer_rejects_invalid_hex() {
  const char *json =
      "{"
      "\"process\":\"eboot.bin\","
      "\"name\":\"Demo\","
      "\"mods\":[{"
      "\"name\":\"Broken\","
      "\"memory\":[{"
      "\"offset\":\"10\","
      "\"on\":\"ABG\","
      "\"off\":\"00\""
      "}]"
      "}]"
      "}";
  static onion_cheat_file_t file;

  TEST_ASSERT_EQ_INT(-1, load_buf("json", json, file));
  onion_cheat_file_clear(&file);
  return 0;
}

int test_parse_json_ignores_out_of_range_section() {
  const char *json =
      "{"
      "\"process\":\"eboot.bin\","
      "\"name\":\"Demo\","
      "\"mods\":[{"
      "\"name\":\"Patch\","
      "\"memory\":[{"
      "\"offset\":\"10\","
      "\"on\":\"AA\","
      "\"off\":\"BB\","
      "\"section\":99"
      "}]"
      "}]"
      "}";
  static onion_cheat_file_t file;

  TEST_ASSERT_EQ_INT(0, load_buf("json", json, file));
  TEST_ASSERT_EQ_INT(0, file.cheats[0].patches[0].section);
  onion_cheat_file_clear(&file);
  return 0;
}

int test_parse_xml_buffer_success() {
  const char *xml =
      "<Trainer Process=\"eboot.bin\" Game=\"Demo\" Moder=\"Yharnam\">"
      "<Cheat Text=\"Infinite Ammo\" Description=\"No reload\">"
      "<Cheatline><Offset>1234</Offset><Section>2</Section>"
      "<ValueOn>AA-BB</ValueOn><ValueOff>00-11</ValueOff></Cheatline>"
      "</Cheat>"
      "</Trainer>";
  static onion_cheat_file_t file;
  const uint8_t on_expected[] = {0xAA, 0xBB};
  const uint8_t off_expected[] = {0x00, 0x11};

  TEST_ASSERT_EQ_INT(0, load_buf("shn", xml, file));
  TEST_ASSERT_STREQ("eboot.bin", file.process);
  TEST_ASSERT_STREQ("Demo", file.name);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(file.author_count));
  TEST_ASSERT_STREQ("Yharnam", file.authors[0]);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(file.cheat_count));
  TEST_ASSERT_STREQ("Infinite Ammo", file.cheats[0].name);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(file.cheats[0].patch_count));
  TEST_ASSERT_EQ_INT(2, file.cheats[0].patches[0].section);
  TEST_ASSERT_EQ_U64(0x1234, file.cheats[0].patches[0].offset);
  TEST_ASSERT_MEMEQ(on_expected, file.cheats[0].patches[0].on,
                    sizeof(on_expected));
  TEST_ASSERT_MEMEQ(off_expected, file.cheats[0].patches[0].off,
                    sizeof(off_expected));
  onion_cheat_file_clear(&file);
  return 0;
}

int test_parse_xml_unescapes_ampersand() {
  const char *xml =
      "<Trainer Process=\"eboot.bin\" Game=\"Ratchet &amp; Clank\" "
      "Moder=\"Alice &amp; Bob\">"
      "<Cheat Text=\"Inf Health &amp; Ammo\" Description=\"HP &amp; ammo\">"
      "<Cheatline><Offset>10</Offset>"
      "<ValueOn>AA</ValueOn><ValueOff>BB</ValueOff></Cheatline>"
      "</Cheat>"
      "</Trainer>";
  static onion_cheat_file_t file;

  TEST_ASSERT_EQ_INT(0, load_buf("shn", xml, file));
  TEST_ASSERT_STREQ("Ratchet & Clank", file.name);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(file.author_count));
  TEST_ASSERT_STREQ("Alice & Bob", file.authors[0]);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(file.cheat_count));
  TEST_ASSERT_STREQ("Inf Health & Ammo", file.cheats[0].name);
  TEST_ASSERT_STREQ("HP & ammo", file.cheats[0].description);
  onion_cheat_file_clear(&file);
  return 0;
}

int test_parse_xml_ignores_out_of_range_section() {
  const char *xml =
      "<Trainer Process=\"eboot.bin\" Game=\"Demo\">"
      "<Cheat Text=\"Patch\">"
      "<Cheatline><Offset>10</Offset><Section>99</Section>"
      "<ValueOn>AA</ValueOn><ValueOff>BB</ValueOff></Cheatline>"
      "</Cheat>"
      "</Trainer>";
  static onion_cheat_file_t file;

  TEST_ASSERT_EQ_INT(0, load_buf("shn", xml, file));
  TEST_ASSERT_EQ_INT(0, file.cheats[0].patches[0].section);
  onion_cheat_file_clear(&file);
  return 0;
}

int test_load_file_mc4_success() {
  const char *xml =
      "<Trainer Process=\"eboot.bin\" Game=\"Demo\">"
      "<Cheat Text=\"Master Code\" Description=\"base\">"
      "<Cheatline><Offset>2000</Offset><ValueOn>AA</ValueOn>"
      "<ValueOff>BB</ValueOff></Cheatline>"
      "</Cheat>"
      "</Trainer>";
  char path[256];
  static onion_cheat_file_t file;

  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_mc4_file(xml, path, sizeof(path)));
  TEST_ASSERT_EQ_INT(0, load_path(path, file));
  TEST_ASSERT_STREQ("eboot.bin", file.process);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(file.cheat_count));
  TEST_ASSERT_STREQ("Master Code", file.cheats[0].name);
  onion_cheat_file_clear(&file);
  onion_test_remove_file(path);
  return 0;
}

int test_fixture_json_file_loads() {
  char path[512];
  static onion_cheat_file_t file;

  TEST_ASSERT_EQ_INT(
      0, onion_test_fixture_path("fixtures/cheats/PPSA26344_01.008.000.json",
                                 path, sizeof(path)));
  TEST_ASSERT_EQ_INT(0, load_path(path, file));
  TEST_ASSERT_STREQ("Ghost of Y\u014dtei", file.name);
  TEST_ASSERT_STREQ("eboot.bin", file.process);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(file.author_count));
  TEST_ASSERT_STREQ("Yharnam", file.authors[0]);
  TEST_ASSERT_EQ_INT(3, static_cast<int>(file.cheat_count));
  TEST_ASSERT_STREQ("Infi Health", file.cheats[0].name);
  onion_cheat_file_clear(&file);
  return 0;
}

int test_fixture_shn_file_loads() {
  char path[512];
  static onion_cheat_file_t file;

  TEST_ASSERT_EQ_INT(
      0, onion_test_fixture_path("fixtures/cheats/PPSA21159_01.001.000.shn",
                                 path, sizeof(path)));
  TEST_ASSERT_EQ_INT(0, load_path(path, file));
  TEST_ASSERT_STREQ("Silent Hill f", file.name);
  TEST_ASSERT_STREQ("eboot.bin", file.process);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(file.author_count));
  TEST_ASSERT_STREQ("Yharnam", file.authors[0]);
  TEST_ASSERT_EQ_INT(9, static_cast<int>(file.cheat_count));
  TEST_ASSERT_STREQ("Infi Health", file.cheats[0].name);
  onion_cheat_file_clear(&file);
  return 0;
}

int test_fixture_mc4_file_loads() {
  char path[512];
  static onion_cheat_file_t file;

  TEST_ASSERT_EQ_INT(
      0, onion_test_fixture_path("fixtures/cheats/PPSA08710_01.005.000.mc4",
                                 path, sizeof(path)));
  TEST_ASSERT_EQ_INT(0, load_path(path, file));
  TEST_ASSERT_STREQ("eboot.bin", file.process);
  TEST_ASSERT_TRUE(file.cheat_count > 0);
  TEST_ASSERT_TRUE(file.name[0] != '\0');
  TEST_ASSERT_TRUE(file.cheats[0].name[0] != '\0');
  onion_cheat_file_clear(&file);
  return 0;
}

int test_fixture_shnext_file_loads() {
  char path[512];
  static onion_cheat_file_t file;
  uint8_t nop3[3];
  uint8_t nop6[6];
  uint8_t nop8[8];

  std::memset(nop3, 0x90, 3);
  std::memset(nop6, 0x90, 6);
  std::memset(nop8, 0x90, 8);

  TEST_ASSERT_EQ_INT(
      0, onion_test_fixture_path(
             "fixtures/cheats/"
             "Assassins-Creed-Mirage_PPSA07230_01.012.000_Aigars_Uze.ShnExt",
             path, sizeof(path)));
  TEST_ASSERT_EQ_INT(0, load_path(path, file));
  TEST_ASSERT_STREQ("Assassin's Creed Mirage", file.name);
  TEST_ASSERT_STREQ("eboot.bin", file.process);
  TEST_ASSERT_EQ_INT(4, static_cast<int>(file.cheat_count));

  TEST_ASSERT_STREQ("Enemys Dont See You", file.cheats[0].name);
  TEST_ASSERT_STREQ("by Anonyme", file.cheats[0].description);
  TEST_ASSERT_TRUE(file.author_count >= 1);
  TEST_ASSERT_STREQ("Anonyme", file.authors[0]);
  TEST_ASSERT_STREQ("eboot.bin", file.cheats[0].module_name);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(file.cheats[0].patch_count));
  TEST_ASSERT_EQ_U64(109230218, file.cheats[0].patches[0].offset);
  TEST_ASSERT_TRUE(!file.cheats[0].patches[0].is_asm);
  TEST_ASSERT_EQ_INT(3, static_cast<int>(file.cheats[0].patches[0].on_len));
  TEST_ASSERT_MEMEQ(nop3, file.cheats[0].patches[0].on, 3);
  TEST_ASSERT_TRUE(file.cheats[0].patches[0].off_len > 0);

  TEST_ASSERT_STREQ("Infinite Air Under Water", file.cheats[1].name);
  TEST_ASSERT_EQ_U64(88497722, file.cheats[1].patches[0].offset);
  TEST_ASSERT_TRUE(!file.cheats[1].patches[0].is_asm);
  TEST_ASSERT_EQ_INT(8, static_cast<int>(file.cheats[1].patches[0].on_len));
  TEST_ASSERT_MEMEQ(nop8, file.cheats[1].patches[0].on, 8);

  TEST_ASSERT_STREQ("Infinite Health", file.cheats[2].name);
  TEST_ASSERT_EQ_U64(31563972, file.cheats[2].patches[0].offset);
  TEST_ASSERT_EQ_INT(6, static_cast<int>(file.cheats[2].patches[0].on_len));
  TEST_ASSERT_MEMEQ(nop6, file.cheats[2].patches[0].on, 6);

  TEST_ASSERT_STREQ("Infinite Stamina", file.cheats[3].name);
  TEST_ASSERT_EQ_U64(39201365, file.cheats[3].patches[0].offset);
  TEST_ASSERT_TRUE(file.cheats[3].patches[0].on_len > 0);
  TEST_ASSERT_TRUE(file.cheats[3].patches[0].off_len > 0);

  onion_cheat_file_clear(&file);
  return 0;
}

int test_factory_extension_routing() {
  auto json = CheatParserFactory::createByFormat("json");
  auto shn = CheatParserFactory::createByFormat(".shn");
  auto mc4 = CheatParserFactory::createByFormat("MC4");
  auto shnext = CheatParserFactory::createByFormat("ShnExt");
  auto shnext_path = CheatParserFactory::createByPath("demo.SHNEXT");

  TEST_ASSERT_STREQ("json", json->name());
  TEST_ASSERT_STREQ("shn", shn->name());
  TEST_ASSERT_STREQ("mc4", mc4->name());
  TEST_ASSERT_STREQ("ShnExt", shnext->name());
  TEST_ASSERT_STREQ("ShnExt", shnext_path->name());
  return 0;
}

} // namespace

extern "C" int test_cheat_parsers_suite(void) {
  int failures = 0;

  failures += onion_test_run("cheat json parse success",
                             test_parse_json_buffer_success);
  failures += onion_test_run("cheat json authors alias and dedup",
                             test_parse_json_authors_alias_and_dedup);
  failures += onion_test_run("cheat json decodes escaped strings",
                             test_parse_json_decodes_escaped_strings);
  failures += onion_test_run("cheat json rejects trailing garbage",
                             test_parse_json_rejects_trailing_garbage);
  failures += onion_test_run("cheat json rejects invalid hex",
                             test_parse_json_buffer_rejects_invalid_hex);
  failures += onion_test_run("cheat json ignores out-of-range section",
                             test_parse_json_ignores_out_of_range_section);
  failures +=
      onion_test_run("cheat xml parse success", test_parse_xml_buffer_success);
  failures += onion_test_run("cheat xml unescapes ampersand",
                             test_parse_xml_unescapes_ampersand);
  failures += onion_test_run("cheat xml ignores out-of-range section",
                             test_parse_xml_ignores_out_of_range_section);
  failures +=
      onion_test_run("cheat mc4 load success", test_load_file_mc4_success);
  failures +=
      onion_test_run("fixture json file loads", test_fixture_json_file_loads);
  failures +=
      onion_test_run("fixture shn file loads", test_fixture_shn_file_loads);
  failures +=
      onion_test_run("fixture mc4 file loads", test_fixture_mc4_file_loads);
  failures += onion_test_run("fixture shnext file loads",
                             test_fixture_shnext_file_loads);
  failures += onion_test_run("parser factory extension routing",
                             test_factory_extension_routing);
  return failures;
}
