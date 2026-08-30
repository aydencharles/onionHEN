/* Host unit tests for cheat flat-name helpers (no PS5 FS required). */
#include "test_harness.h"

#include "cheats/runtime.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static size_t flatten_progress_calls;
static size_t flatten_progress_completed;
static size_t flatten_progress_total;
static int flatten_cancel_after_first;
static int flatten_cancel_requested;

static void capture_flatten_progress(size_t completed, size_t total,
                                     void *user) {
  (void)user;
  ++flatten_progress_calls;
  flatten_progress_completed = completed;
  flatten_progress_total = total;
  if (flatten_cancel_after_first && completed >= 1) {
    flatten_cancel_requested = 1;
  }
}

static int should_cancel_flatten(void *user) {
  (void)user;
  return flatten_cancel_requested;
}

static int write_text(const char *path, const char *text) {
  FILE *file = fopen(path, "wb");
  if (file == NULL) {
    return -1;
  }
  if (fwrite(text, 1, strlen(text), file) != strlen(text)) {
    fclose(file);
    return -1;
  }
  return fclose(file);
}

static int test_flatten_progress(void) {
  const char *source = ONION_DATA_ROOT "/flatten-progress";
  const char *json_dir = ONION_DATA_ROOT "/flatten-progress/json";
  const char *shn_dir = ONION_DATA_ROOT "/flatten-progress/shn";
  const char *first_source =
      ONION_DATA_ROOT "/flatten-progress/json/CUSA99991_01.00.json";
  const char *second_source =
      ONION_DATA_ROOT "/flatten-progress/shn/CUSA99992_01.00.shn";
  const char *ignored_source =
      ONION_DATA_ROOT "/flatten-progress/json/readme.txt";
  const char *first_dest = ONION_CHEATS_DIR "/CUSA99991_01.00.json";
  const char *second_dest = ONION_CHEATS_DIR "/CUSA99992_01.00.shn";

  (void)mkdir(ONION_DATA_ROOT, 0777);
  (void)mkdir(ONION_CHEATS_DIR, 0777);
  (void)mkdir(source, 0777);
  (void)mkdir(json_dir, 0777);
  (void)mkdir(shn_dir, 0777);
  TEST_ASSERT_EQ_INT(0, write_text(first_source, "{}"));
  TEST_ASSERT_EQ_INT(0, write_text(second_source, "fixture"));
  TEST_ASSERT_EQ_INT(0, write_text(ignored_source, "ignored"));

  flatten_progress_calls = 0;
  flatten_progress_completed = 0;
  flatten_progress_total = 0;
  TEST_ASSERT_EQ_INT(ONION_CHEAT_FLATTEN_OK,
                     onion_cheat_flatten_install_tree_cancellable(
                         source, capture_flatten_progress, NULL, NULL, NULL));
  TEST_ASSERT_EQ_U64(2, flatten_progress_total);
  TEST_ASSERT_EQ_U64(flatten_progress_total, flatten_progress_completed);
  TEST_ASSERT_EQ_U64(3, flatten_progress_calls);

  unlink(first_source);
  unlink(second_source);
  unlink(ignored_source);
  unlink(first_dest);
  unlink(second_dest);
  rmdir(json_dir);
  rmdir(shn_dir);
  rmdir(source);
  return 0;
}

static int test_flatten_cancel_between_files(void) {
  const char *source = ONION_DATA_ROOT "/flatten-cancel";
  const char *json_dir = ONION_DATA_ROOT "/flatten-cancel/json";
  const char *shn_dir = ONION_DATA_ROOT "/flatten-cancel/shn";
  const char *first_source =
      ONION_DATA_ROOT "/flatten-cancel/json/CUSA99993_01.00.json";
  const char *second_source =
      ONION_DATA_ROOT "/flatten-cancel/shn/CUSA99994_01.00.shn";
  const char *first_dest = ONION_CHEATS_DIR "/CUSA99993_01.00.json";
  const char *second_dest = ONION_CHEATS_DIR "/CUSA99994_01.00.shn";
  int installed_count;

  (void)mkdir(ONION_DATA_ROOT, 0777);
  (void)mkdir(ONION_CHEATS_DIR, 0777);
  (void)mkdir(source, 0777);
  (void)mkdir(json_dir, 0777);
  (void)mkdir(shn_dir, 0777);
  (void)unlink(first_dest);
  (void)unlink(second_dest);
  TEST_ASSERT_EQ_INT(0, write_text(first_source, "{}"));
  TEST_ASSERT_EQ_INT(0, write_text(second_source, "fixture"));

  flatten_progress_calls = 0;
  flatten_progress_completed = 0;
  flatten_progress_total = 0;
  flatten_cancel_after_first = 1;
  flatten_cancel_requested = 0;
  TEST_ASSERT_EQ_INT(
      ONION_CHEAT_FLATTEN_CANCELLED,
      onion_cheat_flatten_install_tree_cancellable(
          source, capture_flatten_progress, NULL, should_cancel_flatten, NULL));
  TEST_ASSERT_EQ_U64(2, flatten_progress_total);
  TEST_ASSERT_EQ_U64(1, flatten_progress_completed);
  installed_count = (access(first_dest, F_OK) == 0 ? 1 : 0) +
                    (access(second_dest, F_OK) == 0 ? 1 : 0);
  TEST_ASSERT_EQ_INT(1, installed_count);

  flatten_cancel_after_first = 0;
  flatten_cancel_requested = 0;
  unlink(first_source);
  unlink(second_source);
  unlink(first_dest);
  unlink(second_dest);
  rmdir(json_dir);
  rmdir(shn_dir);
  rmdir(source);
  return 0;
}

static int test_flatten_keeps_original_names(void) {
  const char *source = ONION_DATA_ROOT "/flatten-keep";
  const char *json_dir = ONION_DATA_ROOT "/flatten-keep/json";
  const char *mc4_dir = ONION_DATA_ROOT "/flatten-keep/mc4";
  const char *json_name = "CUSA00016_01.00_e4bf73bd.json";
  const char *process_name = "CUSA00018_01.21_default_mp.elf_8624072e.json";
  const char *mc4_name = "PPSA12345_01.000.000_9a1bc234.mc4";
  char json_source[256];
  char process_source[256];
  char mc4_source[256];
  char json_dest[256];
  char process_dest[256];
  char mc4_dest[256];

  (void)mkdir(ONION_DATA_ROOT, 0777);
  (void)mkdir(ONION_CHEATS_DIR, 0777);
  (void)mkdir(source, 0777);
  (void)mkdir(json_dir, 0777);
  (void)mkdir(mc4_dir, 0777);
  snprintf(json_source, sizeof(json_source), "%s/%s", json_dir, json_name);
  snprintf(process_source, sizeof(process_source), "%s/%s", json_dir,
           process_name);
  snprintf(mc4_source, sizeof(mc4_source), "%s/%s", mc4_dir, mc4_name);
  snprintf(json_dest, sizeof(json_dest), ONION_CHEATS_DIR "/%s", json_name);
  snprintf(process_dest, sizeof(process_dest), ONION_CHEATS_DIR "/%s",
           process_name);
  snprintf(mc4_dest, sizeof(mc4_dest), ONION_CHEATS_DIR "/%s", mc4_name);
  unlink(json_dest);
  unlink(process_dest);
  unlink(mc4_dest);
  TEST_ASSERT_EQ_INT(0, write_text(json_source, "{}"));
  TEST_ASSERT_EQ_INT(0, write_text(process_source, "{}"));
  TEST_ASSERT_EQ_INT(0, write_text(mc4_source, "mc4"));

  TEST_ASSERT_EQ_INT(ONION_CHEAT_FLATTEN_OK,
                     onion_cheat_flatten_install_tree_cancellable(
                         source, NULL, NULL, NULL, NULL));
  TEST_ASSERT_TRUE(access(json_dest, F_OK) == 0);
  TEST_ASSERT_TRUE(access(process_dest, F_OK) == 0);
  TEST_ASSERT_TRUE(access(mc4_dest, F_OK) == 0);

  unlink(json_source);
  unlink(process_source);
  unlink(mc4_source);
  unlink(json_dest);
  unlink(process_dest);
  unlink(mc4_dest);
  rmdir(json_dir);
  rmdir(mc4_dir);
  rmdir(source);
  return 0;
}

static int test_flatten_ignores_nested_and_root_files(void) {
  const char *source = ONION_DATA_ROOT "/flatten-ignore";
  const char *json_dir = ONION_DATA_ROOT "/flatten-ignore/json";
  const char *nested_dir = ONION_DATA_ROOT "/flatten-ignore/json/nested";
  const char *root_source =
      ONION_DATA_ROOT "/flatten-ignore/CUSA99990_01.00.json";
  const char *nested_source =
      ONION_DATA_ROOT "/flatten-ignore/json/nested/CUSA99991_01.00.json";
  const char *kept_source =
      ONION_DATA_ROOT "/flatten-ignore/json/CUSA99992_01.00.json";
  const char *root_dest = ONION_CHEATS_DIR "/CUSA99990_01.00.json";
  const char *nested_dest = ONION_CHEATS_DIR "/CUSA99991_01.00.json";
  const char *kept_dest = ONION_CHEATS_DIR "/CUSA99992_01.00.json";

  (void)mkdir(ONION_DATA_ROOT, 0777);
  (void)mkdir(ONION_CHEATS_DIR, 0777);
  (void)mkdir(source, 0777);
  (void)mkdir(json_dir, 0777);
  (void)mkdir(nested_dir, 0777);
  unlink(root_dest);
  unlink(nested_dest);
  unlink(kept_dest);
  TEST_ASSERT_EQ_INT(0, write_text(root_source, "{}"));
  TEST_ASSERT_EQ_INT(0, write_text(nested_source, "{}"));
  TEST_ASSERT_EQ_INT(0, write_text(kept_source, "{}"));

  TEST_ASSERT_EQ_INT(ONION_CHEAT_FLATTEN_OK,
                     onion_cheat_flatten_install_tree_cancellable(
                         source, NULL, NULL, NULL, NULL));
  TEST_ASSERT_TRUE(access(root_dest, F_OK) != 0);
  TEST_ASSERT_TRUE(access(nested_dest, F_OK) != 0);
  TEST_ASSERT_TRUE(access(kept_dest, F_OK) == 0);

  unlink(root_source);
  unlink(nested_source);
  unlink(kept_source);
  unlink(kept_dest);
  rmdir(nested_dir);
  rmdir(json_dir);
  rmdir(source);
  return 0;
}

static int test_match_ext_known(void) {
  char ext[16];
  size_t extension_start = 0;

  TEST_ASSERT_EQ_INT(1, onion_cheat_match_ext("CUSA12345_01.00.json", ext,
                                              sizeof(ext)));
  TEST_ASSERT_STREQ("json", ext);

  TEST_ASSERT_EQ_INT(1, onion_cheat_match_ext("PPSA00001_01.001.000.SHN", ext,
                                              sizeof(ext)));
  TEST_ASSERT_STREQ("shn", ext);

  TEST_ASSERT_EQ_INT(1, onion_cheat_match_ext("game.mc4", ext, sizeof(ext)));
  TEST_ASSERT_STREQ("mc4", ext);

  TEST_ASSERT_EQ_INT(1, onion_cheat_match_ext("x.ShnExt", ext, sizeof(ext)));
  TEST_ASSERT_STREQ("ShnExt", ext);

  TEST_ASSERT_EQ_INT(1, onion_cheat_match_ext("x.shnext", ext, sizeof(ext)));
  TEST_ASSERT_STREQ("ShnExt", ext);

  TEST_ASSERT_EQ_INT(
      0, onion_cheat_extension_rank("CUSA12345_01.00.JSON", &extension_start));
  TEST_ASSERT_EQ_INT(15, (int)extension_start);
  TEST_ASSERT_STREQ("json", onion_cheat_extension_for_rank(0));
  TEST_ASSERT_STREQ("ShnExt", onion_cheat_extension_for_rank(3));
  TEST_ASSERT_TRUE(onion_cheat_extension_for_rank(-1) == NULL);
  TEST_ASSERT_TRUE(onion_cheat_extension_for_rank(4) == NULL);
  return 0;
}

static int test_match_ext_reject(void) {
  char ext[16] = "keep";

  TEST_ASSERT_EQ_INT(0, onion_cheat_match_ext("readme.txt", ext, sizeof(ext)));
  TEST_ASSERT_EQ_INT(0, onion_cheat_match_ext("json", ext, sizeof(ext)));
  TEST_ASSERT_EQ_INT(0, onion_cheat_match_ext(NULL, ext, sizeof(ext)));
  TEST_ASSERT_EQ_INT(0, onion_cheat_match_ext("", ext, sizeof(ext)));
  TEST_ASSERT_EQ_INT(0, onion_cheat_match_ext(".json", ext, sizeof(ext)));
  return 0;
}

static int test_parse_filename_parts(void) {
  onion_cheat_filename_t parts;

  TEST_ASSERT_EQ_INT(
      0, onion_cheat_parse_filename("CUSA05786_01.04.json", &parts));
  TEST_ASSERT_STREQ("CUSA05786", parts.title_id);
  TEST_ASSERT_STREQ("01.04", parts.version);
  TEST_ASSERT_STREQ("", parts.process);
  TEST_ASSERT_STREQ("", parts.source_id);
  TEST_ASSERT_STREQ("", parts.suffix);
  TEST_ASSERT_EQ_INT(0, parts.extension_rank);

  TEST_ASSERT_EQ_INT(0, onion_cheat_parse_filename(
                            "PPSA17168_01.004.000_97905f51.json", &parts));
  TEST_ASSERT_STREQ("PPSA17168", parts.title_id);
  TEST_ASSERT_STREQ("01.004.000", parts.version);
  TEST_ASSERT_STREQ("", parts.process);
  TEST_ASSERT_STREQ("97905f51", parts.source_id);
  TEST_ASSERT_STREQ("97905f51", parts.suffix);
  TEST_ASSERT_EQ_INT(0, parts.extension_rank);

  TEST_ASSERT_EQ_INT(
      0, onion_cheat_parse_filename("CUSA05786_01.04_eboot.bin.json", &parts));
  TEST_ASSERT_STREQ("eboot.bin", parts.process);
  TEST_ASSERT_STREQ("", parts.source_id);
  TEST_ASSERT_STREQ("eboot.bin", parts.suffix);

  TEST_ASSERT_EQ_INT(
      0, onion_cheat_parse_filename("cusa12345-01.00.shn", &parts));
  TEST_ASSERT_STREQ("CUSA12345", parts.title_id);
  TEST_ASSERT_STREQ("01.00", parts.version);
  TEST_ASSERT_STREQ("", parts.suffix);
  TEST_ASSERT_EQ_INT(1, parts.extension_rank);

  TEST_ASSERT_EQ_INT(0, onion_cheat_parse_filename(
                            "CUSA00018_01.21_default.elf_fc14a673.json",
                            &parts));
  TEST_ASSERT_STREQ("CUSA00018", parts.title_id);
  TEST_ASSERT_STREQ("01.21", parts.version);
  TEST_ASSERT_STREQ("default.elf", parts.process);
  TEST_ASSERT_STREQ("fc14a673", parts.source_id);

  TEST_ASSERT_EQ_INT(
      0, onion_cheat_parse_filename(
             "CUSA02343_01.00_big2-ps4_Shipping.elf_8feca873.json", &parts));
  TEST_ASSERT_STREQ("big2-ps4_Shipping.elf", parts.process);
  TEST_ASSERT_STREQ("8feca873", parts.source_id);

  TEST_ASSERT_EQ_INT(0, onion_cheat_parse_filename(
                            "CUSA00025_01.00_default_mp.elf_123854e1.shn",
                            &parts));
  TEST_ASSERT_STREQ("default_mp.elf", parts.process);
  TEST_ASSERT_STREQ("123854e1", parts.source_id);

  TEST_ASSERT_EQ_INT(
      0, onion_cheat_parse_filename("CUSA00016_01.00_e4bf73bd.json", &parts));
  TEST_ASSERT_STREQ("CUSA00016", parts.title_id);
  TEST_ASSERT_STREQ("01.00", parts.version);
  TEST_ASSERT_STREQ("", parts.process);
  TEST_ASSERT_STREQ("e4bf73bd", parts.source_id);

  TEST_ASSERT_EQ_INT(0, onion_cheat_parse_filename(
                            "CUSA00018_01.21_default_mp.elf_8624072e.json",
                            &parts));
  TEST_ASSERT_STREQ("default_mp.elf", parts.process);
  TEST_ASSERT_STREQ("8624072e", parts.source_id);

  TEST_ASSERT_EQ_INT(
      0, onion_cheat_parse_filename("PPSA12345_01.000.000_9a1bc234.mc4",
                                    &parts));
  TEST_ASSERT_STREQ("PPSA12345", parts.title_id);
  TEST_ASSERT_STREQ("01.000.000", parts.version);
  TEST_ASSERT_STREQ("9a1bc234", parts.source_id);
  TEST_ASSERT_EQ_INT(2, parts.extension_rank);

  TEST_ASSERT_EQ_INT(
      0, onion_cheat_parse_filename("SLUS00551_01.00_A74D915B.json", &parts));
  TEST_ASSERT_STREQ("SLUS00551", parts.title_id);
  TEST_ASSERT_STREQ("a74d915b", parts.source_id);

  TEST_ASSERT_EQ_INT(
      0, onion_cheat_parse_filename(
             "Assassins-Creed-Mirage_PPSA07230_01.012.000_Aigars_Uze.ShnExt",
             &parts));
  TEST_ASSERT_STREQ("PPSA07230", parts.title_id);
  TEST_ASSERT_STREQ("01.012.000", parts.version);
  TEST_ASSERT_STREQ("", parts.process);
  TEST_ASSERT_STREQ("", parts.source_id);
  TEST_ASSERT_STREQ("Aigars_Uze", parts.suffix);
  TEST_ASSERT_EQ_INT(3, parts.extension_rank);

  TEST_ASSERT_EQ_INT(-1, onion_cheat_parse_filename("readme.txt", &parts));
  TEST_ASSERT_EQ_INT(-1, onion_cheat_parse_filename(NULL, &parts));
  TEST_ASSERT_EQ_INT(-1, onion_cheat_parse_filename("CUSA05786_01.04.json",
                                                    NULL));
  return 0;
}

static int test_legacy_eboot_alias(void) {
  TEST_ASSERT_EQ_INT(1, onion_cheat_is_source_id("97905f51"));
  TEST_ASSERT_EQ_INT(1, onion_cheat_is_source_id("A74D915B"));
  TEST_ASSERT_EQ_INT(0, onion_cheat_is_source_id("default"));
  TEST_ASSERT_EQ_INT(0, onion_cheat_is_source_id("97905f5"));
  TEST_ASSERT_EQ_INT(1, onion_cheat_is_eboot_process("eboot"));
  TEST_ASSERT_EQ_INT(1, onion_cheat_is_eboot_process("EBOOT.BIN"));
  TEST_ASSERT_EQ_INT(0, onion_cheat_is_eboot_process("default.elf"));
  TEST_ASSERT_EQ_INT(1, onion_cheat_is_legacy_eboot_alias("97905f51"));
  TEST_ASSERT_EQ_INT(1, onion_cheat_is_legacy_eboot_alias("eboot.bin"));
  TEST_ASSERT_EQ_INT(1, onion_cheat_is_legacy_eboot_alias("Aigars_Uze"));
  TEST_ASSERT_EQ_INT(0, onion_cheat_is_legacy_eboot_alias("worker.bin"));
  TEST_ASSERT_EQ_INT(0, onion_cheat_is_legacy_eboot_alias("tllr-boot.bin"));
  TEST_ASSERT_EQ_INT(0, onion_cheat_is_legacy_eboot_alias("default.elf"));
  TEST_ASSERT_EQ_INT(0, onion_cheat_is_legacy_eboot_alias(
                            "default.elf_060fa01b"));
  TEST_ASSERT_EQ_INT(0, onion_cheat_is_legacy_eboot_alias(""));
  TEST_ASSERT_EQ_INT(0, onion_cheat_is_legacy_eboot_alias(NULL));
  return 0;
}

static onion_cheat_filename_t parse_or_empty(const char *name) {
  onion_cheat_filename_t parts;
  memset(&parts, 0, sizeof(parts));
  (void)onion_cheat_parse_filename(name, &parts);
  return parts;
}

static int test_filename_compatible_and_compare(void) {
  const onion_cheat_filename_t generic =
      parse_or_empty("CUSA00018_01.21.json");
  const onion_cheat_filename_t sourced =
      parse_or_empty("CUSA00018_01.21_6584f95f.json");
  const onion_cheat_filename_t other_source =
      parse_or_empty("CUSA00018_01.21_8bb68d84.json");
  const onion_cheat_filename_t process_source = parse_or_empty(
      "CUSA00018_01.21_default.elf_fc14a673.json");
  const onion_cheat_filename_t process_only =
      parse_or_empty("CUSA00018_01.21_default.elf.json");

  TEST_ASSERT_STREQ("6584f95f", sourced.source_id);

  TEST_ASSERT_EQ_INT(1, onion_cheat_filename_compatible(&generic, "eboot.bin"));
  TEST_ASSERT_EQ_INT(1,
                     onion_cheat_filename_compatible(&generic, "default.elf"));
  TEST_ASSERT_EQ_INT(1, onion_cheat_filename_compatible(&sourced, "eboot.bin"));
  TEST_ASSERT_EQ_INT(1,
                     onion_cheat_filename_compatible(&sourced, "default.elf"));
  TEST_ASSERT_EQ_INT(
      1, onion_cheat_filename_compatible(&process_source, "default.elf"));
  TEST_ASSERT_EQ_INT(
      0, onion_cheat_filename_compatible(&process_source, "eboot.bin"));
  TEST_ASSERT_EQ_INT(
      0, onion_cheat_filename_compatible(&process_only, "worker.bin"));

  TEST_ASSERT_TRUE(onion_cheat_filename_compare(&process_source, "proc.json",
                                                &generic, "generic.json") < 0);
  TEST_ASSERT_TRUE(onion_cheat_filename_compare(&generic, "generic.json",
                                                &sourced, "sourced.json") < 0);
  TEST_ASSERT_TRUE(onion_cheat_filename_compare(
                       &sourced, "CUSA00018_01.21_6584f95f.json", &other_source,
                       "CUSA00018_01.21_8bb68d84.json") < 0);
  return 0;
}

static int test_normalize_version(void) {
  char out[32];

  onion_cheat_normalize_version("01.004.000", out, sizeof(out));
  TEST_ASSERT_STREQ("01.004.000", out);

  onion_cheat_normalize_version("1.0 (beta)", out, sizeof(out));
  TEST_ASSERT_STREQ("1.0__beta_", out);

  onion_cheat_normalize_version("a/b\\c", out, sizeof(out));
  TEST_ASSERT_STREQ("a_b_c", out);

  onion_cheat_normalize_version(NULL, out, sizeof(out));
  TEST_ASSERT_STREQ("", out);

  onion_cheat_normalize_version("x", out, 1); /* only room for NUL */
  TEST_ASSERT_STREQ("", out);

  onion_cheat_normalize_version("ab", out, 2); /* one char + NUL */
  TEST_ASSERT_STREQ("a", out);

  onion_cheat_normalize_filename_token("eboot/bin", out, sizeof(out));
  TEST_ASSERT_STREQ("eboot_bin", out);
  return 0;
}

int test_cheat_flatten_suite(void) {
  int failures = 0;
  failures += onion_test_run("flatten.match_ext_known", test_match_ext_known);
  failures += onion_test_run("flatten.match_ext_reject", test_match_ext_reject);
  failures += onion_test_run("flatten.progress", test_flatten_progress);
  failures += onion_test_run("flatten.cancel_between_files",
                             test_flatten_cancel_between_files);
  failures += onion_test_run("flatten.keeps_original_names",
                             test_flatten_keeps_original_names);
  failures += onion_test_run("flatten.ignores_nested_and_root",
                             test_flatten_ignores_nested_and_root_files);
  failures += onion_test_run("flatten.parse_filename", test_parse_filename_parts);
  failures +=
      onion_test_run("flatten.legacy_eboot_alias", test_legacy_eboot_alias);
  failures += onion_test_run("flatten.filename_compatible_compare",
                             test_filename_compatible_and_compare);
  failures +=
      onion_test_run("flatten.normalize_version", test_normalize_version);
  return failures;
}
