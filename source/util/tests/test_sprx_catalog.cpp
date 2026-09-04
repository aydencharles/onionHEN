#include "test_harness.h"
#include "test_support.h"

#include <onion/sprx_catalog.hpp>

#include <string>
#include <unistd.h>
#include <vector>

namespace {

constexpr char kManifest[] =
    "[plugin.base-runtime]\n"
    "path=/data/OnionHEN/sprx/base-runtime.sprx\n"
    "exact_title_ids=CUSA12345\n"
    "priority=10\n"
    "\n"
    "[plugin.overlay]\n"
    "path=/data/OnionHEN/sprx/overlay.sprx\n"
    "exact_title_ids=CUSA12345\n"
    "auto_start=true\n"
    "priority=100\n"
    "dependencies=base-runtime\n"
    "\n"
    "[plugin.telemetry]\n"
    "path=/data/OnionHEN/sprx/telemetry.sprx\n"
    "exact_title_ids=CUSA12345\n"
    "auto_start=true\n"
    "priority=200\n"
    "\n"
    "[plugin.native]\n"
    "path=/data/OnionHEN/sprx/native.prx\n"
    "title_id_prefixes=PPSA\n"
    "auto_start=true\n";

int test_parse_and_startup_order() {
  onion::sprx::SprxCatalog catalog;
  std::vector<onion::sprx::SprxCatalogIssue> issues;
  TEST_ASSERT_TRUE(catalog.parse(kManifest, &issues));
  TEST_ASSERT_EQ_INT(0, static_cast<int>(issues.size()));
  TEST_ASSERT_EQ_INT(4, static_cast<int>(catalog.entries().size()));
  const auto *overlay = catalog.find("overlay");
  TEST_ASSERT_TRUE(overlay != nullptr);
  TEST_ASSERT_TRUE(catalog.matches(*overlay, "CUSA12345"));
  TEST_ASSERT_TRUE(!catalog.matches(*overlay, "PPSA00001"));

  const auto order = catalog.startup_order("CUSA12345", &issues);
  TEST_ASSERT_EQ_INT(0, static_cast<int>(issues.size()));
  TEST_ASSERT_EQ_INT(3, static_cast<int>(order.size()));
  TEST_ASSERT_STREQ("telemetry", order[0]->id.c_str());
  TEST_ASSERT_STREQ("base-runtime", order[1]->id.c_str());
  TEST_ASSERT_STREQ("overlay", order[2]->id.c_str());

  const auto native_order = catalog.startup_order("PPSA00001", &issues);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(native_order.size()));
  TEST_ASSERT_STREQ("native", native_order[0]->id.c_str());
  return 0;
}

int test_duplicate_and_unknown_keys_rejected() {
  onion::sprx::SprxCatalog catalog;
  std::vector<onion::sprx::SprxCatalogIssue> issues;
  TEST_ASSERT_TRUE(!catalog.parse(
      "[plugin.one]\n"
      "path=/data/one.sprx\n"
      "exact_title_ids=CUSA12345\n"
      "path=/data/two.sprx\n",
      &issues));
  TEST_ASSERT_TRUE(!issues.empty());
  TEST_ASSERT_EQ_INT(0, static_cast<int>(catalog.entries().size()));
  return 0;
}

int test_dependency_errors_rejected_at_ordering() {
  onion::sprx::SprxCatalog catalog;
  std::vector<onion::sprx::SprxCatalogIssue> issues;
  TEST_ASSERT_TRUE(catalog.parse(
      "[plugin.one]\n"
      "path=/data/one.sprx\n"
      "exact_title_ids=CUSA12345\n"
      "auto_start=true\n"
      "dependencies=missing\n",
      &issues));
  TEST_ASSERT_TRUE(catalog.startup_order("CUSA12345", &issues).empty());
  TEST_ASSERT_TRUE(!issues.empty());
  return 0;
}

int test_dependency_cycle_rejected() {
  onion::sprx::SprxCatalog catalog;
  std::vector<onion::sprx::SprxCatalogIssue> issues;
  TEST_ASSERT_TRUE(catalog.parse(
      "[plugin.one]\npath=/data/one.sprx\nexact_title_ids=CUSA12345\n"
      "auto_start=true\ndependencies=two\n"
      "[plugin.two]\npath=/data/two.sprx\nexact_title_ids=CUSA12345\n"
      "dependencies=one\n",
      &issues));
  TEST_ASSERT_TRUE(catalog.startup_order("CUSA12345", &issues).empty());
  TEST_ASSERT_TRUE(!issues.empty());
  return 0;
}

int test_enabled_persists_and_excludes_startup() {
  char path[256] = {};
  const char manifest[] =
      "[plugin.overlay]\n"
      "path=/data/OnionHEN/sprx/overlay.sprx\n"
      "exact_title_ids=CUSA12345\n"
      "auto_start=true\n";
  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_text_file(
                            ".ini", manifest, path, sizeof(path)));

  onion::sprx::SprxCatalogStore store;
  std::vector<onion::sprx::SprxCatalogIssue> issues;
  TEST_ASSERT_TRUE(store.load(path, &issues));
  TEST_ASSERT_EQ_INT(1, static_cast<int>(store.snapshot()
                                             .startup_order("CUSA12345", &issues)
                                             .size()));
  TEST_ASSERT_TRUE(store.set_enabled("overlay", false));
  TEST_ASSERT_EQ_INT(0, static_cast<int>(store.snapshot()
                                             .startup_order("CUSA12345", &issues)
                                             .size()));

  onion::sprx::SprxCatalogStore reloaded;
  TEST_ASSERT_TRUE(reloaded.load(path, &issues));
  const auto inventory = reloaded.inventory();
  TEST_ASSERT_EQ_INT(1, static_cast<int>(inventory.size()));
  TEST_ASSERT_TRUE(!inventory[0].enabled);
  TEST_ASSERT_EQ_INT(0, static_cast<int>(reloaded.snapshot()
                                             .startup_order("CUSA12345", &issues)
                                             .size()));
  unlink(path);
  return 0;
}

int test_remove_persists_catalog_entry_only() {
  char path[256] = {};
  const char manifest[] =
      "[plugin.overlay]\n"
      "path=/data/OnionHEN/sprx/overlay.sprx\n"
      "exact_title_ids=CUSA12345\n";
  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_text_file(
                            ".ini", manifest, path, sizeof(path)));
  onion::sprx::SprxCatalogStore store;
  TEST_ASSERT_TRUE(store.load(path));
  TEST_ASSERT_TRUE(store.remove("overlay"));
  onion::sprx::SprxCatalogStore reloaded;
  TEST_ASSERT_TRUE(reloaded.load(path));
  TEST_ASSERT_TRUE(reloaded.inventory().empty());
  unlink(path);
  return 0;
}

} // namespace

extern "C" int test_sprx_catalog_suite(void) {
  int failures = 0;
  failures += onion_test_run("sprx_catalog_parse_order", test_parse_and_startup_order);
  failures += onion_test_run("sprx_catalog_invalid_keys", test_duplicate_and_unknown_keys_rejected);
  failures += onion_test_run("sprx_catalog_missing_dependency", test_dependency_errors_rejected_at_ordering);
  failures += onion_test_run("sprx_catalog_dependency_cycle", test_dependency_cycle_rejected);
  failures += onion_test_run("sprx_catalog_enabled_persists", test_enabled_persists_and_excludes_startup);
  failures += onion_test_run("sprx_catalog_remove_persists", test_remove_persists_catalog_entry_only);
  return failures;
}
