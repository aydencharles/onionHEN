/* Host unit tests for libonion_settings schema (no PS5 SDK). */
#include "test_harness.h"

#include <onion/settings.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

static std::string temp_ini_path() {
  char tmpl[] = "/tmp/onion-settings-XXXXXX.ini";
  /* mkstemps needs suffix length 4 for ".ini" */
  int fd = mkstemps(tmpl, 4);
  if (fd < 0) {
    return {};
  }
  close(fd);
  unlink(tmpl); /* write fresh via settings_save_file */
  return std::string(tmpl);
}

static int test_defaults_and_serialize_keys(void) {
  onion::Settings s{};
  std::string text = onion::settings_serialize(s);

  TEST_ASSERT_TRUE(text.find("[meta]") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("schema_version=1") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("[toolbox]") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("language=system") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("[startup]") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("open_after_load=none") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("[logging]") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("level=info") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("temperature_threshold_celsius=77") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(text.find("resume_reinject_delay_seconds") ==
                   std::string::npos);
  TEST_ASSERT_TRUE(text.find("[rest_mode]") == std::string::npos);
  TEST_ASSERT_TRUE(text.find("stop_utility_daemon_on_entry") ==
                   std::string::npos);
  TEST_ASSERT_TRUE(text.find("close_running_game_on_entry") ==
                   std::string::npos);
  TEST_ASSERT_TRUE(text.find("[app_jailbreak]\n") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("[app_jailbreak]\n# enabled controls the App "
                                 "lifecycle and sandbox event listeners. "
                                 "When false,\n"
                                 "# OnionHEN does not register either listener.\n"
                                 "# Available values: true, false\n"
                                 "enabled=true\n") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("edge=top") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("align=center") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("background=true") != std::string::npos);
  TEST_ASSERT_TRUE(
      text.find("exact_title_ids=ITEM00001,NPXS39041,PKGI13337,PKGI12345,"
                "TOOL00001") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("title_id_prefixes=LAPY") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("mirror=auto") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("memory_backend") == std::string::npos);
  TEST_ASSERT_TRUE(text.find("[kstuff]\n") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("autoload=true") != std::string::npos);
  return 0;
}

static int test_roundtrip_file(void) {
  std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());

  onion::Settings in{};
  in.fan_threshold = 55;
  in.cheats_shortcut_opt = 2;
  in.startup_open_after_load = onion::kStartupOpenHomeMenu;
  in.ui_lang = onion::kUiLanguageEn;
  in.log_level = onion::kLogLevelDebug;

  TEST_ASSERT_TRUE(onion::settings_save_file(path.c_str(), in));

  onion::Settings out{};
  TEST_ASSERT_TRUE(onion::settings_load_file(path.c_str(), &out));

  TEST_ASSERT_EQ_INT(55, out.fan_threshold);
  TEST_ASSERT_EQ_INT(2, out.cheats_shortcut_opt);
  TEST_ASSERT_EQ_INT(onion::kStartupOpenHomeMenu,
                     out.startup_open_after_load);
  TEST_ASSERT_EQ_INT(onion::kUiLanguageEn, out.ui_lang);
  TEST_ASSERT_EQ_INT(onion::kLogLevelDebug, out.log_level);
  TEST_ASSERT_EQ_INT(onion::kSettingsSchemaVersion, out.schema_version);

  unlink(path.c_str());
  return 0;
}

static int test_missing_file_defaults(void) {
  onion::Settings out{};
  out.fan_threshold = 1;
  TEST_ASSERT_TRUE(
      !onion::settings_load_file("/tmp/onion-settings-does-not-exist-xyz.ini",
                                 &out));
  /* load_file resets to defaults even on failure */
  TEST_ASSERT_EQ_INT(77, out.fan_threshold);
  return 0;
}

static int test_full_schema_roundtrip(void) {
  std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());

  onion::Settings in{};
  in.startup_open_after_load = onion::kStartupOpenHomeMenu;
  in.cheats_mirror = onion::kCheatsMirrorCnb;
  in.app_jailbreak_enabled = false;
  in.debug_app_jb_msg = true;
  in.display_tids = true;
  in.onionhen_game_opts = false;
  in.enable_fan_speed = true;
  in.fan_threshold = 90;
  in.overlay_enabled = false;
  in.overlay_background = false;
  in.overlay_ram = false;
  in.overlay_cpu = false;
  in.overlay_gpu = false;
  in.overlay_fps = false;
  in.overlay_ip = true;
  in.overlay_fan = true;
  in.all_cpu_usage = true;
  in.overlay_pos = 2;
  in.overlay_align = onion::kOverlayAlignRight;
  in.cheats_shortcut_opt = 4;
  in.toolbox_shortcut_opt = 2;
  in.ui_lang = onion::kUiLanguageZhHans;
  in.kstuff_autoload = false;
  in.app_jailbreak_allowlist.exact_title_ids = {};
  in.app_jailbreak_allowlist.exact_title_ids[0] = "ITEM00001";
  in.app_jailbreak_allowlist.exact_title_ids[1] = "CUSA12345";
  in.app_jailbreak_allowlist.exact_title_id_count = 2;
  in.app_jailbreak_allowlist.title_id_prefixes = {};
  in.app_jailbreak_allowlist.title_id_prefixes[0] = "TEST";
  in.app_jailbreak_allowlist.title_id_prefix_count = 1;

  TEST_ASSERT_TRUE(onion::settings_save_file(path.c_str(), in));
  onion::Settings out{};
  TEST_ASSERT_TRUE(onion::settings_load_file(path.c_str(), &out));

  TEST_ASSERT_EQ_INT(in.startup_open_after_load, out.startup_open_after_load);
  TEST_ASSERT_TRUE(out.app_jailbreak_enabled == in.app_jailbreak_enabled);
  TEST_ASSERT_TRUE(out.debug_app_jb_msg == in.debug_app_jb_msg);
  TEST_ASSERT_TRUE(out.display_tids == in.display_tids);
  TEST_ASSERT_TRUE(out.onionhen_game_opts == in.onionhen_game_opts);
  TEST_ASSERT_TRUE(out.enable_fan_speed == in.enable_fan_speed);
  TEST_ASSERT_EQ_INT(in.fan_threshold, out.fan_threshold);
  TEST_ASSERT_TRUE(out.overlay_enabled == in.overlay_enabled);
  TEST_ASSERT_TRUE(out.overlay_background == in.overlay_background);
  TEST_ASSERT_TRUE(out.overlay_ram == in.overlay_ram);
  TEST_ASSERT_TRUE(out.overlay_cpu == in.overlay_cpu);
  TEST_ASSERT_TRUE(out.overlay_gpu == in.overlay_gpu);
  TEST_ASSERT_TRUE(out.overlay_fps == in.overlay_fps);
  TEST_ASSERT_TRUE(out.overlay_ip == in.overlay_ip);
  TEST_ASSERT_TRUE(out.overlay_fan == in.overlay_fan);
  TEST_ASSERT_TRUE(out.all_cpu_usage == in.all_cpu_usage);
  TEST_ASSERT_EQ_INT(in.overlay_pos, out.overlay_pos);
  TEST_ASSERT_EQ_INT(in.overlay_align, out.overlay_align);
  TEST_ASSERT_EQ_INT(in.cheats_shortcut_opt, out.cheats_shortcut_opt);
  TEST_ASSERT_EQ_INT(in.toolbox_shortcut_opt, out.toolbox_shortcut_opt);
  TEST_ASSERT_EQ_INT(in.ui_lang, out.ui_lang);
  TEST_ASSERT_EQ_INT(in.cheats_mirror, out.cheats_mirror);
  TEST_ASSERT_TRUE(out.kstuff_autoload == in.kstuff_autoload);
  TEST_ASSERT_EQ_U64(
      in.app_jailbreak_allowlist.exact_title_id_count,
      out.app_jailbreak_allowlist.exact_title_id_count);
  TEST_ASSERT_STREQ(
      "ITEM00001", out.app_jailbreak_allowlist.exact_title_ids[0].c_str());
  TEST_ASSERT_STREQ(
      "CUSA12345", out.app_jailbreak_allowlist.exact_title_ids[1].c_str());
  TEST_ASSERT_EQ_U64(
      1, out.app_jailbreak_allowlist.title_id_prefix_count);
  TEST_ASSERT_STREQ(
      "TEST", out.app_jailbreak_allowlist.title_id_prefixes[0].c_str());

  unlink(path.c_str());
  return 0;
}

static int test_partial_ini_keeps_defaults(void) {
  std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());
  FILE *f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fputs("[meta]\nschema_version=1\n\n[rest_mode]\n"
        "stop_utility_daemon_on_entry=true\n"
        "close_running_game_on_entry=true\n",
        f);
  fclose(f);

  onion::Settings out{};
  TEST_ASSERT_TRUE(onion::settings_load_file(path.c_str(), &out));
  /* Removed keys are ignored; unspecified keys stay at defaults. */
  TEST_ASSERT_EQ_INT(onion::kStartupOpenNone, out.startup_open_after_load);
  TEST_ASSERT_EQ_INT(77, out.fan_threshold);
  TEST_ASSERT_TRUE(out.overlay_enabled);
  TEST_ASSERT_TRUE(out.overlay_background);
  TEST_ASSERT_TRUE(out.app_jailbreak_enabled);
  TEST_ASSERT_TRUE(out.kstuff_autoload);
  TEST_ASSERT_EQ_INT(onion::kCheatsMirrorAuto, out.cheats_mirror);
  TEST_ASSERT_EQ_U64(5, out.app_jailbreak_allowlist.exact_title_id_count);
  TEST_ASSERT_STREQ("ITEM00001",
                    out.app_jailbreak_allowlist.exact_title_ids[0].c_str());
  TEST_ASSERT_EQ_U64(1, out.app_jailbreak_allowlist.title_id_prefix_count);
  TEST_ASSERT_STREQ("LAPY",
                    out.app_jailbreak_allowlist.title_id_prefixes[0].c_str());

  unlink(path.c_str());
  return 0;
}

static int test_startup_open_after_load_parse_policy(void) {
  std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());

  FILE *f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fputs("[meta]\nschema_version=1\n\n[startup]\n"
        "open_after_load=home_menu\n",
        f);
  fclose(f);

  onion::Settings configured{};
  TEST_ASSERT_TRUE(onion::settings_load_file(path.c_str(), &configured));
  TEST_ASSERT_EQ_INT(onion::kStartupOpenHomeMenu,
                     configured.startup_open_after_load);

  f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fputs("[meta]\nschema_version=1\n\n[startup]\n"
        "open_after_load=settings\n",
        f);
  fclose(f);

  onion::Settings invalid{};
  TEST_ASSERT_TRUE(onion::settings_load_file(path.c_str(), &invalid));
  TEST_ASSERT_EQ_INT(onion::kStartupOpenNone, invalid.startup_open_after_load);

  unlink(path.c_str());
  return 0;
}

static int test_serialize_contains_overlay_keys(void) {
  onion::Settings s{};
  s.overlay_enabled = false;
  s.overlay_background = false;
  s.overlay_ip = true;
  s.all_cpu_usage = true;
  s.overlay_pos = 2;
  std::string text = onion::settings_serialize(s);
  TEST_ASSERT_TRUE(text.find("overlay_fps=") == std::string::npos);
  TEST_ASSERT_TRUE(text.find("show_fps=") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("enabled=false") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("background=false") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("show_ip_address=true") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("cpu_usage_mode=per_core") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("edge=bottom") != std::string::npos);
  return 0;
}

static int test_empty_file_loads_defaults(void) {
  std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());
  FILE *f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fclose(f);

  onion::Settings out{};
  /* empty file: load may succeed or fall back — either way defaults applied */
  (void)onion::settings_load_file(path.c_str(), &out);
  TEST_ASSERT_EQ_INT(77, out.fan_threshold);
  unlink(path.c_str());
  return 0;
}

static int test_app_jailbreak_allowlist_parse_policy(void) {
  std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());

  FILE *f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fputs("[meta]\nschema_version=1\n\n[app_jailbreak]\n"
        "enabled=false\n"
        "exact_title_ids=CUSA12345, CUSA12345, TEST00001\n"
        "title_id_prefixes=ABCD,TEST\n",
        f);
  fclose(f);

  onion::Settings configured{};
  TEST_ASSERT_TRUE(onion::settings_load_file(path.c_str(), &configured));
  TEST_ASSERT_TRUE(!configured.app_jailbreak_enabled);
  TEST_ASSERT_EQ_U64(
      2, configured.app_jailbreak_allowlist.exact_title_id_count);
  TEST_ASSERT_STREQ(
      "CUSA12345",
      configured.app_jailbreak_allowlist.exact_title_ids[0].c_str());
  TEST_ASSERT_STREQ(
      "TEST00001",
      configured.app_jailbreak_allowlist.exact_title_ids[1].c_str());
  TEST_ASSERT_EQ_U64(
      2, configured.app_jailbreak_allowlist.title_id_prefix_count);

  f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fputs("[meta]\nschema_version=1\n\n[app_jailbreak]\n"
        "exact_title_ids=none\n"
        "title_id_prefixes=none\n",
        f);
  fclose(f);

  onion::Settings disabled{};
  TEST_ASSERT_TRUE(onion::settings_load_file(path.c_str(), &disabled));
  TEST_ASSERT_EQ_U64(0,
                     disabled.app_jailbreak_allowlist.exact_title_id_count);
  TEST_ASSERT_EQ_U64(0,
                     disabled.app_jailbreak_allowlist.title_id_prefix_count);

  f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fputs("[meta]\nschema_version=1\n\n[app_jailbreak]\n"
        "exact_title_ids=ITEM00001,*\n"
        "title_id_prefixes=*\n",
        f);
  fclose(f);

  onion::Settings invalid{};
  TEST_ASSERT_TRUE(onion::settings_load_file(path.c_str(), &invalid));
  /* Invalid replacement values retain the compiled safe defaults. */
  TEST_ASSERT_EQ_U64(5,
                     invalid.app_jailbreak_allowlist.exact_title_id_count);
  TEST_ASSERT_STREQ(
      "ITEM00001", invalid.app_jailbreak_allowlist.exact_title_ids[0].c_str());
  TEST_ASSERT_EQ_U64(1,
                     invalid.app_jailbreak_allowlist.title_id_prefix_count);
  TEST_ASSERT_STREQ(
      "LAPY", invalid.app_jailbreak_allowlist.title_id_prefixes[0].c_str());

  unlink(path.c_str());
  return 0;
}

static int test_settings_store_snapshot_update(void) {
  onion::SettingsStore store;
  onion::Settings a = store.snapshot();
  TEST_ASSERT_EQ_INT(77, a.fan_threshold);

  store.update([](onion::Settings &s) {
    s.fan_threshold = 60;
    s.enable_fan_speed = true;
  });
  onion::Settings b = store.snapshot();
  TEST_ASSERT_EQ_INT(60, b.fan_threshold);
  TEST_ASSERT_TRUE(b.enable_fan_speed);

  onion::Settings c{};
  c.overlay_ip = true;
  store.store(c);
  onion::Settings d = store.snapshot();
  TEST_ASSERT_TRUE(d.overlay_ip);
  TEST_ASSERT_TRUE(d.enable_fan_speed == false); /* full replace */
  return 0;
}

static int test_config_mtime_helpers(void) {
  /* No twin paths on host test machine → newest is 0, not newer. */
  TEST_ASSERT_TRUE(onion::settings_config_newest_mtime() == 0 ||
                   onion::settings_config_newest_mtime() >= 0);
  const time_t n = onion::settings_config_newest_mtime();
  if (n == 0) {
    TEST_ASSERT_TRUE(!onion::settings_config_is_newer_than(0));
  } else {
    TEST_ASSERT_TRUE(onion::settings_config_is_newer_than(n - 1));
    TEST_ASSERT_TRUE(!onion::settings_config_is_newer_than(n));
  }
  return 0;
}

/* An unrecognised level must fall back to the default, not to off. */
static int test_log_level_invalid_falls_back(void) {
  const std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());

  FILE *f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fputs("[meta]\nschema_version=1\n\n[logging]\nlevel=verbose\n", f);
  fclose(f);

  onion::Settings out{};
  TEST_ASSERT_TRUE(onion::settings_load_file(path.c_str(), &out));
  TEST_ASSERT_EQ_INT(onion::kLogLevelInfo, out.log_level);

  unlink(path.c_str());
  return 0;
}

static int test_language_ar_roundtrip(void) {
  const std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());

  onion::Settings in{};
  in.ui_lang = onion::kUiLanguageAr;
  TEST_ASSERT_TRUE(onion::settings_serialize(in).find("language=ar") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(onion::settings_save_file(path.c_str(), in));

  onion::Settings out{};
  TEST_ASSERT_TRUE(onion::settings_load_file(path.c_str(), &out));
  TEST_ASSERT_EQ_INT(onion::kUiLanguageAr, out.ui_lang);

  FILE *f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fputs("[meta]\nschema_version=1\n\n[toolbox]\nlanguage=ar-SA\n", f);
  fclose(f);
  TEST_ASSERT_TRUE(onion::settings_load_file(path.c_str(), &out));
  TEST_ASSERT_EQ_INT(onion::kUiLanguageAr, out.ui_lang);

  unlink(path.c_str());
  return 0;
}

static int test_language_new_locales_roundtrip(void) {
  const std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());

  struct {
    int value;
    const char *canonical;
    const char *alias;
  } const cases[] = {
      {onion::kUiLanguageZhHant, "zh-Hant", "zh-TW"},
      {onion::kUiLanguageJa, "ja", "ja-JP"},
      {onion::kUiLanguageFr, "fr", "fr-FR"},
      {onion::kUiLanguageDe, "de", "deutsch"},
      {onion::kUiLanguageKo, "ko", "ko-KR"},
      {onion::kUiLanguageEs, "es", "es-MX"},
      {onion::kUiLanguagePtBr, "pt-BR", "portuguese"},
      {onion::kUiLanguageIt, "it", "italiano"},
      {onion::kUiLanguageRu, "ru", "ru-RU"},
      {onion::kUiLanguagePl, "pl", "polski"},
      {onion::kUiLanguageTh, "th", "thai"},
  };

  for (const auto &c : cases) {
    onion::Settings in{};
    in.ui_lang = c.value;
    const std::string text = onion::settings_serialize(in);
    TEST_ASSERT_TRUE(text.find(std::string("language=") + c.canonical) !=
                     std::string::npos);
    TEST_ASSERT_TRUE(onion::settings_save_file(path.c_str(), in));

    onion::Settings out{};
    TEST_ASSERT_TRUE(onion::settings_load_file(path.c_str(), &out));
    TEST_ASSERT_EQ_INT(c.value, out.ui_lang);

    FILE *f = fopen(path.c_str(), "w");
    TEST_ASSERT_TRUE(f != nullptr);
    fprintf(f, "[meta]\nschema_version=1\n\n[toolbox]\nlanguage=%s\n",
            c.alias);
    fclose(f);
    TEST_ASSERT_TRUE(onion::settings_load_file(path.c_str(), &out));
    TEST_ASSERT_EQ_INT(c.value, out.ui_lang);
  }

  unlink(path.c_str());
  return 0;
}

static int test_v0_0_10_config_keeps_kstuff_autoload(void) {
  std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());
  FILE *f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fputs("[meta]\n"
        "schema_version=1\n"
        "\n[toolbox]\n"
        "language=zh-Hans\n"
        "\n[kstuff]\n"
        "autoload=true\n",
        f);
  fclose(f);

  onion::Settings out{};
  TEST_ASSERT_TRUE(onion::settings_load_file(path.c_str(), &out));
  TEST_ASSERT_TRUE(out.kstuff_autoload);

  unlink(path.c_str());
  return 0;
}

static int test_clamp_fan_threshold(void) {
  TEST_ASSERT_EQ_INT(onion::kFanAutomaticThresholdCelsius,
                     onion::Settings{}.fan_threshold);
  TEST_ASSERT_EQ_INT(onion::kFanThresholdMinCelsius,
                     onion::clamp_fan_threshold(-5));
  TEST_ASSERT_EQ_INT(onion::kFanThresholdMinCelsius,
                     onion::clamp_fan_threshold(onion::kFanThresholdMinCelsius));
  TEST_ASSERT_EQ_INT(onion::kFanAutomaticThresholdCelsius,
                     onion::clamp_fan_threshold(
                         onion::kFanAutomaticThresholdCelsius));
  TEST_ASSERT_EQ_INT(onion::kFanThresholdMaxCelsius,
                     onion::clamp_fan_threshold(onion::kFanThresholdMaxCelsius));
  TEST_ASSERT_EQ_INT(onion::kFanThresholdMaxCelsius,
                     onion::clamp_fan_threshold(140));
  return 0;
}

static int test_overlay_edge_and_align(void) {
  const std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());

  struct {
    const char *edge;
    int expected_pos;
    const char *edge_canonical;
  } const edges[] = {
      {"top", 0, "top"},
      {"bottom", 2, "bottom"},
  };
  for (const auto &c : edges) {
    FILE *f = fopen(path.c_str(), "w");
    TEST_ASSERT_TRUE(f != nullptr);
    fprintf(f, "[meta]\nschema_version=1\n\n[overlay]\nedge=%s\n", c.edge);
    fclose(f);
    onion::Settings out{};
    TEST_ASSERT_TRUE(onion::settings_load_file(path.c_str(), &out));
    TEST_ASSERT_EQ_INT(c.expected_pos, out.overlay_pos);
    TEST_ASSERT_EQ_INT(onion::kOverlayAlignCenter, out.overlay_align);
    TEST_ASSERT_TRUE(onion::settings_serialize(out).find(
                         std::string("edge=") + c.edge_canonical) !=
                     std::string::npos);
  }

  struct {
    const char *align;
    int expected;
    const char *canonical;
  } const aligns[] = {
      {"left", onion::kOverlayAlignLeft, "left"},
      {"center", onion::kOverlayAlignCenter, "center"},
      {"right", onion::kOverlayAlignRight, "right"},
  };
  for (const auto &c : aligns) {
    FILE *f = fopen(path.c_str(), "w");
    TEST_ASSERT_TRUE(f != nullptr);
    fprintf(f,
            "[meta]\nschema_version=1\n\n[overlay]\nedge=top\nalign=%s\n",
            c.align);
    fclose(f);
    onion::Settings out{};
    TEST_ASSERT_TRUE(onion::settings_load_file(path.c_str(), &out));
    TEST_ASSERT_EQ_INT(0, out.overlay_pos);
    TEST_ASSERT_EQ_INT(c.expected, out.overlay_align);
    TEST_ASSERT_TRUE(onion::settings_serialize(out).find(
                         std::string("align=") + c.canonical) !=
                     std::string::npos);
  }

  unlink(path.c_str());
  return 0;
}

extern "C" int test_settings_suite(void) {
  int failures = 0;
  failures += onion_test_run("settings_defaults_serialize", test_defaults_and_serialize_keys);
  failures += onion_test_run("settings_roundtrip_file", test_roundtrip_file);
  failures += onion_test_run("settings_missing_file_defaults", test_missing_file_defaults);
  failures += onion_test_run("settings_full_schema_roundtrip", test_full_schema_roundtrip);
  failures += onion_test_run("settings_partial_ini_defaults", test_partial_ini_keeps_defaults);
  failures += onion_test_run("settings_startup_open_after_load",
                             test_startup_open_after_load_parse_policy);
  failures += onion_test_run("settings_serialize_overlay_keys", test_serialize_contains_overlay_keys);
  failures += onion_test_run("settings_overlay_edge_and_align",
                             test_overlay_edge_and_align);
  failures += onion_test_run("settings_empty_file_defaults", test_empty_file_loads_defaults);
  failures += onion_test_run("settings_app_jailbreak_allowlist",
                             test_app_jailbreak_allowlist_parse_policy);
  failures += onion_test_run("settings_store_snapshot_update",
                             test_settings_store_snapshot_update);
  failures += onion_test_run("settings_config_mtime_helpers", test_config_mtime_helpers);
  failures += onion_test_run("settings_log_level_invalid",
                             test_log_level_invalid_falls_back);
  failures += onion_test_run("settings_language_ar", test_language_ar_roundtrip);
  failures += onion_test_run("settings_language_new_locales",
                             test_language_new_locales_roundtrip);
  failures += onion_test_run("settings_v0_0_10_kstuff_autoload",
                             test_v0_0_10_config_keeps_kstuff_autoload);
  failures += onion_test_run("settings_clamp_fan_threshold",
                             test_clamp_fan_threshold);
  return failures;
}
