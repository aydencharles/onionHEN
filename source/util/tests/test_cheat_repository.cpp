#include <onion/fs.h>

#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

#include <unistd.h>

#include "cheats/cheat_repository.hpp"
#include "cheats/runtime.h"
#include "test_harness.h"

using onion::cheats::CheatRepository;

namespace {

class ScopedCheatFiles {
public:
  ScopedCheatFiles(std::initializer_list<const char *> names) {
    CheatRepository::ensureCheatsDir();
    for (const char *name : names) {
      paths_.push_back(path(name));
      ::unlink(paths_.back().c_str());
    }
  }

  ~ScopedCheatFiles() {
    for (const std::string &file : paths_) {
      ::unlink(file.c_str());
    }
  }

  bool create(const char *name) const {
    return touch_file(path(name).c_str());
  }

  static std::string path(const char *name) {
    return std::string(ONION_CHEATS_DIR) + "/" + name;
  }

private:
  std::vector<std::string> paths_;
};

game_context_t makeGame(const char *title_id, const char *version,
                        const char *process) {
  game_context_t game{};
  std::snprintf(game.title_id, sizeof(game.title_id), "%s", title_id);
  std::snprintf(game.version, sizeof(game.version), "%s", version);
  std::snprintf(game.process_name, sizeof(game.process_name), "%s", process);
  return game;
}

int test_compatibility_alias_for_eboot() {
  constexpr const char *alias = "PPSA17168_01.004.000_97905f51.json";
  ScopedCheatFiles files({alias});
  TEST_ASSERT_TRUE(files.create(alias));

  const game_context_t game =
      makeGame("PPSA17168", "01.004.000", "eboot.bin");
  const std::string expected = ScopedCheatFiles::path(alias);
  const std::string actual = CheatRepository::resolvePath(game);
  TEST_ASSERT_STREQ(expected.c_str(), actual.c_str());
  return 0;
}

int test_standard_name_has_priority() {
  constexpr const char *alias = "PPSA17169_01.004.000_97905f51.json";
  constexpr const char *standard = "PPSA17169_01.004.000.json";
  ScopedCheatFiles files({alias, standard});
  TEST_ASSERT_TRUE(files.create(alias));

  const game_context_t game =
      makeGame("PPSA17169", "01.004.000", "eboot.bin");
  const std::string alias_path = ScopedCheatFiles::path(alias);
  std::string actual = CheatRepository::resolvePath(game);
  TEST_ASSERT_STREQ(alias_path.c_str(), actual.c_str());

  TEST_ASSERT_TRUE(files.create(standard));
  const std::string standard_path = ScopedCheatFiles::path(standard);
  actual = CheatRepository::resolvePath(game);
  TEST_ASSERT_STREQ(standard_path.c_str(), actual.c_str());
  return 0;
}

int test_process_name_has_priority() {
  constexpr const char *alias = "PPSA17170_01.004.000_97905f51.json";
  constexpr const char *process = "PPSA17170_01.004.000_eboot.bin.json";
  ScopedCheatFiles files({alias, process});
  TEST_ASSERT_TRUE(files.create(alias));
  TEST_ASSERT_TRUE(files.create(process));

  const game_context_t game =
      makeGame("PPSA17170", "01.004.000", "eboot.bin");
  const std::string expected = ScopedCheatFiles::path(process);
  const std::string actual = CheatRepository::resolvePath(game);
  TEST_ASSERT_STREQ(expected.c_str(), actual.c_str());
  return 0;
}

int test_source_id_generic_matches_processes() {
  constexpr const char *alias = "PPSA17171_01.004.000_97905f51.json";
  ScopedCheatFiles files({alias});
  TEST_ASSERT_TRUE(files.create(alias));

  const game_context_t game =
      makeGame("PPSA17171", "01.004.000", "worker.bin");
  const std::string expected = ScopedCheatFiles::path(alias);
  const std::string actual = CheatRepository::resolvePath(game);
  TEST_ASSERT_STREQ(expected.c_str(), actual.c_str());
  return 0;
}

int test_multiple_source_ids_pick_lexicographic() {
  constexpr const char *first = "PPSA17172_01.004.000_11111111.json";
  constexpr const char *second = "PPSA17172_01.004.000_22222222.json";
  ScopedCheatFiles files({first, second});
  TEST_ASSERT_TRUE(files.create(first));
  TEST_ASSERT_TRUE(files.create(second));

  const game_context_t game =
      makeGame("PPSA17172", "01.004.000", "eboot.bin");
  const std::string expected = ScopedCheatFiles::path(first);
  const std::string actual = CheatRepository::resolvePath(game);
  TEST_ASSERT_STREQ(expected.c_str(), actual.c_str());
  return 0;
}

int test_resolve_paths_returns_all_formats() {
  constexpr const char *json = "PPSA17182_01.004.000_aaaaaaa1.json";
  constexpr const char *shn = "PPSA17182_01.004.000_bbbbbbb2.shn";
  constexpr const char *mc4 = "PPSA17182_01.004.000_ccccccc3.mc4";
  ScopedCheatFiles files({json, shn, mc4});
  TEST_ASSERT_TRUE(files.create(json));
  TEST_ASSERT_TRUE(files.create(shn));
  TEST_ASSERT_TRUE(files.create(mc4));

  const std::vector<std::string> paths = CheatRepository::resolvePaths(
      makeGame("PPSA17182", "01.004.000", "eboot.bin"));
  const std::string json_path = ScopedCheatFiles::path(json);
  const std::string shn_path = ScopedCheatFiles::path(shn);
  const std::string mc4_path = ScopedCheatFiles::path(mc4);
  TEST_ASSERT_EQ_INT(3, static_cast<int>(paths.size()));
  TEST_ASSERT_STREQ(json_path.c_str(), paths[0].c_str());
  TEST_ASSERT_STREQ(shn_path.c_str(), paths[1].c_str());
  TEST_ASSERT_STREQ(mc4_path.c_str(), paths[2].c_str());
  return 0;
}

int test_resolve_paths_keep_generic_and_matching_process() {
  constexpr const char *generic = "PPSA17183_01.004.000_aaaaaaa1.json";
  constexpr const char *worker = "PPSA17183_01.004.000_worker.bin_bbbbbbb2.json";
  constexpr const char *other = "PPSA17183_01.004.000_other.bin_ccccccc3.json";
  ScopedCheatFiles files({generic, worker, other});
  TEST_ASSERT_TRUE(files.create(generic));
  TEST_ASSERT_TRUE(files.create(worker));
  TEST_ASSERT_TRUE(files.create(other));

  const std::string generic_path = ScopedCheatFiles::path(generic);
  const std::string worker_path = ScopedCheatFiles::path(worker);
  const std::vector<std::string> eboot = CheatRepository::resolvePaths(
      makeGame("PPSA17183", "01.004.000", "eboot.bin"));
  TEST_ASSERT_EQ_INT(1, static_cast<int>(eboot.size()));
  TEST_ASSERT_STREQ(generic_path.c_str(), eboot[0].c_str());

  const std::vector<std::string> worker_paths = CheatRepository::resolvePaths(
      makeGame("PPSA17183", "01.004.000", "worker.bin"));
  TEST_ASSERT_EQ_INT(2, static_cast<int>(worker_paths.size()));
  TEST_ASSERT_STREQ(worker_path.c_str(), worker_paths[0].c_str());
  TEST_ASSERT_STREQ(generic_path.c_str(), worker_paths[1].c_str());
  return 0;
}

int test_multiple_source_ids_all_resolve() {
  constexpr const char *first = "PPSA17181_01.004.000_11111111.json";
  constexpr const char *second = "PPSA17181_01.004.000_22222222.json";
  ScopedCheatFiles files({first, second});
  TEST_ASSERT_TRUE(files.create(first));
  TEST_ASSERT_TRUE(files.create(second));

  const std::string expected = ScopedCheatFiles::path(first);
  const std::string actual = CheatRepository::resolvePath(
      makeGame("PPSA17181", "01.004.000", "eboot.bin"));
  TEST_ASSERT_STREQ(expected.c_str(), actual.c_str());
  const std::vector<std::string> paths = CheatRepository::resolvePaths(
      makeGame("PPSA17181", "01.004.000", "eboot.bin"));
  TEST_ASSERT_EQ_INT(2, static_cast<int>(paths.size()));
  return 0;
}

int test_deleted_cached_alias_is_rescanned() {
  constexpr const char *first = "PPSA17173_01.004.000_11111111.json";
  constexpr const char *second = "PPSA17173_01.004.000_22222222.json";
  ScopedCheatFiles files({first, second});
  TEST_ASSERT_TRUE(files.create(first));

  const game_context_t game =
      makeGame("PPSA17173", "01.004.000", "eboot.bin");
  std::string expected = ScopedCheatFiles::path(first);
  std::string actual = CheatRepository::resolvePath(game);
  TEST_ASSERT_STREQ(expected.c_str(), actual.c_str());

  TEST_ASSERT_EQ_INT(0, ::unlink(expected.c_str()));
  TEST_ASSERT_TRUE(files.create(second));
  expected = ScopedCheatFiles::path(second);
  actual = CheatRepository::resolvePath(game);
  TEST_ASSERT_STREQ(expected.c_str(), actual.c_str());
  return 0;
}

class ScopedDecoyFiles {
public:
  explicit ScopedDecoyFiles(int count) {
    CheatRepository::ensureCheatsDir();
    paths_.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
      char name[64];
      std::snprintf(name, sizeof(name), "CUSA%05d_01.00.json", i);
      const std::string file = ScopedCheatFiles::path(name);
      ::unlink(file.c_str());
      if (touch_file(file.c_str())) {
        paths_.push_back(file);
      }
    }
  }

  ~ScopedDecoyFiles() {
    for (const std::string &file : paths_) {
      ::unlink(file.c_str());
    }
  }

  int created() const { return static_cast<int>(paths_.size()); }

private:
  std::vector<std::string> paths_;
};

int test_thousands_of_files_do_not_block_standard_name() {
  constexpr const char *standard = "PPSA17174_01.004.000.json";
  ScopedCheatFiles files({standard});
  ScopedDecoyFiles decoys(3000);
  TEST_ASSERT_TRUE(decoys.created() >= 3000);
  TEST_ASSERT_TRUE(files.create(standard));

  const game_context_t game =
      makeGame("PPSA17174", "01.004.000", "eboot.bin");
  const std::string expected = ScopedCheatFiles::path(standard);
  const std::string actual = CheatRepository::resolvePath(game);
  TEST_ASSERT_STREQ(expected.c_str(), actual.c_str());
  return 0;
}

int test_thousands_of_files_do_not_block_compatibility_alias() {
  constexpr const char *alias = "PPSA17175_01.004.000_97905f51.json";
  ScopedCheatFiles files({alias});
  ScopedDecoyFiles decoys(3000);
  TEST_ASSERT_TRUE(decoys.created() >= 3000);
  TEST_ASSERT_TRUE(files.create(alias));

  const game_context_t game =
      makeGame("PPSA17175", "01.004.000", "eboot.bin");
  const std::string expected = ScopedCheatFiles::path(alias);
  const std::string actual = CheatRepository::resolvePath(game);
  TEST_ASSERT_STREQ(expected.c_str(), actual.c_str());
  return 0;
}

int test_unknown_version_is_rejected() {
  constexpr const char *standard = "PPSA17176_01.004.000.json";
  ScopedCheatFiles files({standard});
  TEST_ASSERT_TRUE(files.create(standard));
  TEST_ASSERT_TRUE(
      CheatRepository::resolvePath(makeGame("PPSA17176", "unknown", "eboot.bin"))
          .empty());
  TEST_ASSERT_TRUE(
      CheatRepository::resolvePath(makeGame("PPSA17176", "", "eboot.bin"))
          .empty());
  TEST_ASSERT_TRUE(
      CheatRepository::resolvePath(makeGame("", "01.004.000", "eboot.bin"))
          .empty());
  return 0;
}

int test_json_alias_outranks_shn_alias() {
  constexpr const char *json_alias = "PPSA17177_01.004.000_aaaa.json";
  constexpr const char *shn_alias = "PPSA17177_01.004.000_bbbb.shn";
  ScopedCheatFiles files({json_alias, shn_alias});
  TEST_ASSERT_TRUE(files.create(json_alias));
  TEST_ASSERT_TRUE(files.create(shn_alias));

  const std::string expected = ScopedCheatFiles::path(json_alias);
  const std::string actual = CheatRepository::resolvePath(
      makeGame("PPSA17177", "01.004.000", "eboot.bin"));
  TEST_ASSERT_STREQ(expected.c_str(), actual.c_str());
  return 0;
}

int test_real_process_file_is_not_eboot_alias() {
  constexpr const char *process = "PPSA17178_01.004.000_worker.bin.json";
  ScopedCheatFiles files({process});
  TEST_ASSERT_TRUE(files.create(process));
  TEST_ASSERT_TRUE(CheatRepository::resolvePath(
                       makeGame("PPSA17178", "01.004.000", "eboot.bin"))
                       .empty());

  const std::string expected = ScopedCheatFiles::path(process);
  const std::string actual = CheatRepository::resolvePath(
      makeGame("PPSA17178", "01.004.000", "worker.bin"));
  TEST_ASSERT_STREQ(expected.c_str(), actual.c_str());
  return 0;
}

int test_eboot_without_bin_suffix_uses_alias() {
  constexpr const char *alias = "PPSA17179_01.004.000_97905f51.json";
  ScopedCheatFiles files({alias});
  TEST_ASSERT_TRUE(files.create(alias));

  const std::string expected = ScopedCheatFiles::path(alias);
  const std::string actual = CheatRepository::resolvePath(
      makeGame("PPSA17179", "01.004.000", "eboot"));
  TEST_ASSERT_STREQ(expected.c_str(), actual.c_str());
  return 0;
}

int test_process_scoped_beats_generic_source_id() {
  constexpr const char *generic = "CUSA00018_01.21_6584f95f.json";
  constexpr const char *process =
      "CUSA00018_01.21_default.elf_fc14a673.json";
  ScopedCheatFiles files({generic, process});
  TEST_ASSERT_TRUE(files.create(generic));
  TEST_ASSERT_TRUE(files.create(process));

  const std::string process_path = ScopedCheatFiles::path(process);
  const std::string generic_path = ScopedCheatFiles::path(generic);
  const std::string default_elf = CheatRepository::resolvePath(
      makeGame("CUSA00018", "01.21", "default.elf"));
  const std::string eboot = CheatRepository::resolvePath(
      makeGame("CUSA00018", "01.21", "eboot.bin"));
  TEST_ASSERT_STREQ(process_path.c_str(), default_elf.c_str());
  TEST_ASSERT_STREQ(generic_path.c_str(), eboot.c_str());
  return 0;
}

int test_process_scoped_beats_generic() {
  constexpr const char *generic = "PPSA05686_01.002.000.shn";
  constexpr const char *process = "PPSA05686_01.002.000_tllr-boot.bin.shn";
  ScopedCheatFiles files({generic, process});
  TEST_ASSERT_TRUE(files.create(generic));
  TEST_ASSERT_TRUE(files.create(process));

  const std::string process_path = ScopedCheatFiles::path(process);
  const std::string generic_path = ScopedCheatFiles::path(generic);
  const std::string tllr = CheatRepository::resolvePath(
      makeGame("PPSA05686", "01.002.000", "tllr-boot.bin"));
  const std::string eboot = CheatRepository::resolvePath(
      makeGame("PPSA05686", "01.002.000", "eboot.bin"));
  TEST_ASSERT_STREQ(process_path.c_str(), tllr.c_str());
  TEST_ASSERT_STREQ(generic_path.c_str(), eboot.c_str());
  return 0;
}

int test_generic_fallback_for_unknown_process() {
  constexpr const char *generic = "PPSA05687_01.002.000.json";
  ScopedCheatFiles files({generic});
  TEST_ASSERT_TRUE(files.create(generic));

  const std::string expected = ScopedCheatFiles::path(generic);
  const std::string actual = CheatRepository::resolvePath(
      makeGame("PPSA05687", "01.002.000", "tllr-boot.bin"));
  TEST_ASSERT_STREQ(expected.c_str(), actual.c_str());
  return 0;
}

int test_underscored_process_source_id() {
  constexpr const char *name =
      "CUSA02343_01.00_big2-ps4_Shipping.elf_8feca873.json";
  ScopedCheatFiles files({name});
  TEST_ASSERT_TRUE(files.create(name));

  const std::string expected = ScopedCheatFiles::path(name);
  const std::string actual = CheatRepository::resolvePath(
      makeGame("CUSA02343", "01.00", "big2-ps4_Shipping.elf"));
  TEST_ASSERT_STREQ(expected.c_str(), actual.c_str());
  TEST_ASSERT_TRUE(CheatRepository::resolvePath(
                       makeGame("CUSA02343", "01.00", "eboot.bin"))
                       .empty());
  return 0;
}

int test_game_name_prefix_resolves() {
  constexpr const char *name =
      "Assassins-Creed-Mirage_PPSA07231_01.012.000.ShnExt";
  ScopedCheatFiles files({name});
  TEST_ASSERT_TRUE(files.create(name));

  const std::string expected = ScopedCheatFiles::path(name);
  const std::string actual = CheatRepository::resolvePath(
      makeGame("PPSA07231", "01.012.000", "eboot.bin"));
  TEST_ASSERT_STREQ(expected.c_str(), actual.c_str());
  return 0;
}

int test_file_signature_identity() {
  constexpr const char *name = "PPSA17180_01.004.000.json";
  ScopedCheatFiles files({name});
  TEST_ASSERT_TRUE(files.create(name));

  onion::cheats::FileSignature first;
  onion::cheats::FileSignature second;
  const std::string path = ScopedCheatFiles::path(name);
  TEST_ASSERT_TRUE(CheatRepository::statSignature(path, first));
  TEST_ASSERT_TRUE(CheatRepository::statSignature(path, second));
  TEST_ASSERT_TRUE(first == second);
  TEST_ASSERT_TRUE(first.sameIdentity(second));
  TEST_ASSERT_STREQ(path.c_str(), first.path.c_str());
  TEST_ASSERT_TRUE(CheatRepository::fileExists(path));
  TEST_ASSERT_TRUE(!CheatRepository::fileExists(""));
  return 0;
}

} // namespace

extern "C" int test_cheat_repository_suite(void) {
  int failures = 0;
  failures += onion_test_run("repository.compatibility_alias",
                             test_compatibility_alias_for_eboot);
  failures += onion_test_run("repository.standard_priority",
                             test_standard_name_has_priority);
  failures += onion_test_run("repository.process_priority",
                             test_process_name_has_priority);
  failures += onion_test_run("repository.source_id_generic_process",
                             test_source_id_generic_matches_processes);
  failures += onion_test_run("repository.multiple_source_ids_lexicographic",
                             test_multiple_source_ids_pick_lexicographic);
  failures += onion_test_run("repository.multiple_source_ids_all_resolve",
                             test_multiple_source_ids_all_resolve);
  failures += onion_test_run("repository.resolve_paths_all_formats",
                             test_resolve_paths_returns_all_formats);
  failures += onion_test_run("repository.resolve_paths_process_filter",
                             test_resolve_paths_keep_generic_and_matching_process);
  failures += onion_test_run("repository.deleted_cache_rescan",
                             test_deleted_cached_alias_is_rescanned);
  failures += onion_test_run("repository.thousands_standard_name",
                             test_thousands_of_files_do_not_block_standard_name);
  failures +=
      onion_test_run("repository.thousands_compatibility_alias",
                     test_thousands_of_files_do_not_block_compatibility_alias);
  failures += onion_test_run("repository.unknown_version",
                             test_unknown_version_is_rejected);
  failures += onion_test_run("repository.json_outranks_shn_alias",
                             test_json_alias_outranks_shn_alias);
  failures += onion_test_run("repository.worker_bin_not_eboot_alias",
                             test_real_process_file_is_not_eboot_alias);
  failures += onion_test_run("repository.eboot_without_bin",
                             test_eboot_without_bin_suffix_uses_alias);
  failures += onion_test_run("repository.file_signature",
                             test_file_signature_identity);
  failures += onion_test_run("repository.process_beats_generic_source_id",
                             test_process_scoped_beats_generic_source_id);
  failures += onion_test_run("repository.process_beats_generic",
                             test_process_scoped_beats_generic);
  failures += onion_test_run("repository.generic_fallback_process",
                             test_generic_fallback_for_unknown_process);
  failures += onion_test_run("repository.underscored_process_source_id",
                             test_underscored_process_source_id);
  failures += onion_test_run("repository.game_name_prefix",
                             test_game_name_prefix_resolves);
  return failures;
}
