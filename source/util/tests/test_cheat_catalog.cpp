#include "test_harness.h"

#include "cheats/sync/cheat_catalog_registry.hpp"

#include <cstring>
#include <string>

using onion::cheats::sync::CheatCatalogRegistry;
using onion::cheats::sync::CheatMirrorId;

static int test_primary_id_and_layout(void) {
  const auto &cat = CheatCatalogRegistry::primary();
  TEST_ASSERT_TRUE(cat.id() != nullptr);
  TEST_ASSERT_TRUE(std::strchr(cat.id(), '/') == nullptr);
  TEST_ASSERT_TRUE(cat.defaultBranch() != nullptr);
  TEST_ASSERT_TRUE(cat.defaultBranch()[0] != '\0');

  size_t n = 0;
  const char *const *roots = cat.flattenRoots(&n);
  TEST_ASSERT_TRUE(roots != nullptr);
  TEST_ASSERT_TRUE(n >= 1);
  TEST_ASSERT_STREQ("cheats", roots[0]);

  return 0;
}

static int test_slug_is_owner_repo(void) {
  const auto &cat = CheatCatalogRegistry::primary();
  const char *gh = cat.slugFor(CheatMirrorId::Github);
  const char *cnb = cat.slugFor(CheatMirrorId::Cnb);
  TEST_ASSERT_TRUE(gh != nullptr && std::strchr(gh, '/') != nullptr);
  TEST_ASSERT_TRUE(cnb != nullptr && std::strchr(cnb, '/') != nullptr);
  TEST_ASSERT_TRUE(std::strstr(gh, "https://") == nullptr);
  TEST_ASSERT_STREQ("TeeKay87/HEN-Cheats-Collection", gh);
  TEST_ASSERT_STREQ("PS5-xiexiyu/CN-Cheats-Collection", cnb);
  return 0;
}

static int test_registry_find(void) {
  const auto &cat = CheatCatalogRegistry::primary();
  TEST_ASSERT_TRUE(CheatCatalogRegistry::find(nullptr) == &cat);
  TEST_ASSERT_TRUE(CheatCatalogRegistry::find("") == &cat);
  TEST_ASSERT_TRUE(CheatCatalogRegistry::find(cat.id()) == &cat);
  TEST_ASSERT_TRUE(CheatCatalogRegistry::find("no-such-catalog") == nullptr);
  return 0;
}

extern "C" int test_cheat_catalog_suite(void) {
  int fails = 0;
  fails += onion_test_run("catalog.layout", test_primary_id_and_layout);
  fails += onion_test_run("catalog.slug", test_slug_is_owner_repo);
  fails += onion_test_run("catalog.find", test_registry_find);
  return fails;
}
