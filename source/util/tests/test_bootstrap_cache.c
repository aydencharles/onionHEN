/* Host tests for the unpacker bootstrapper cache (source/unpacker).
 *
 * Locks the contract that keeps boot independent of the external elfldr
 * version: a cache hit returns the verified ELF bytes in RAM, and every
 * failure mode (missing / truncated / tampered / not a file) returns false
 * without touching the caller's pointer, so the unpacker falls back to the
 * embedded LZMA blob.
 *
 * Also covers the two boot-path wrappers bootstrap_acquire() uses: a miss is
 * a NULL return rather than an error, and the refresh path refuses to cache a
 * decompressed size that disagrees with pack time.
 */
#include "test_harness.h"

#include "bootstrap_cache.h"
#include "sha1.h"

#include <onion/fs.h>
#include <onion/tree.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int make_temp_dir(char *buf, size_t buflen) {
  char tmpl[] = "/tmp/onion-cache-XXXXXX";
  if (!mkdtemp(tmpl)) {
    return -1;
  }
  if (strlen(tmpl) + 1 > buflen) {
    return -1;
  }
  memcpy(buf, tmpl, strlen(tmpl) + 1);
  return 0;
}

static const uint8_t kPayload[] = "not-a-real-elf-but-the-bytes-matter";

static int test_load_rejects_bad_args(void) {
  uint8_t digest[SHA1_DIGEST_SIZE];
  uint8_t *out = (uint8_t *)0x1;

  sha1_hash(kPayload, sizeof(kPayload) - 1, digest);

  TEST_ASSERT_TRUE(!bootstrap_cache_load(NULL, sizeof(kPayload) - 1, digest,
                                         &out));
  TEST_ASSERT_TRUE(!bootstrap_cache_load("relative/path", sizeof(kPayload) - 1,
                                         digest, &out));
  TEST_ASSERT_TRUE(
      !bootstrap_cache_load("/tmp/onion-cache-x", 0, digest, &out));
  TEST_ASSERT_TRUE(!bootstrap_cache_load("/tmp/onion-cache-x",
                                         sizeof(kPayload) - 1, NULL, &out));
  TEST_ASSERT_TRUE(!bootstrap_cache_load("/tmp/onion-cache-x",
                                         sizeof(kPayload) - 1, digest, NULL));
  /* Every rejection above must leave the caller's pointer alone. */
  TEST_ASSERT_TRUE(out == (uint8_t *)0x1);
  return 0;
}

static int test_load_missing_and_not_a_file(void) {
  char dir[64];
  uint8_t digest[SHA1_DIGEST_SIZE];
  uint8_t *out = NULL;

  sha1_hash(kPayload, sizeof(kPayload) - 1, digest);
  TEST_ASSERT_EQ_INT(0, make_temp_dir(dir, sizeof(dir)));

  TEST_ASSERT_TRUE(!bootstrap_cache_load("/tmp/onion-cache-missing-xyz-91c",
                                         sizeof(kPayload) - 1, digest, &out));
  TEST_ASSERT_TRUE(out == NULL);

  /* A directory is not a cache hit, even when the size argument matches. */
  TEST_ASSERT_TRUE(!bootstrap_cache_load(dir, sizeof(kPayload) - 1, digest,
                                         &out));
  TEST_ASSERT_TRUE(out == NULL);

  TEST_ASSERT_TRUE(rmtree(dir));
  return 0;
}

static int test_load_size_mismatch(void) {
  char dir[64];
  char path[128];
  uint8_t digest[SHA1_DIGEST_SIZE];
  uint8_t *out = NULL;

  TEST_ASSERT_EQ_INT(0, make_temp_dir(dir, sizeof(dir)));
  snprintf(path, sizeof(path), "%s/onionhen.elf", dir);
  TEST_ASSERT_TRUE(write_file_atomic(path, kPayload, sizeof(kPayload) - 1));
  sha1_hash(kPayload, sizeof(kPayload) - 1, digest);

  /* One byte short, one byte long, and empty. */
  TEST_ASSERT_TRUE(
      !bootstrap_cache_load(path, sizeof(kPayload) - 2, digest, &out));
  TEST_ASSERT_TRUE(
      !bootstrap_cache_load(path, sizeof(kPayload), digest, &out));
  TEST_ASSERT_TRUE(!bootstrap_cache_load(path, 1, digest, &out));
  TEST_ASSERT_TRUE(out == NULL);

  TEST_ASSERT_TRUE(rmtree(dir));
  return 0;
}

static int test_load_sha1_mismatch(void) {
  char dir[64];
  char path[128];
  uint8_t digest[SHA1_DIGEST_SIZE];
  uint8_t *out = NULL;

  TEST_ASSERT_EQ_INT(0, make_temp_dir(dir, sizeof(dir)));
  snprintf(path, sizeof(path), "%s/onionhen.elf", dir);
  TEST_ASSERT_TRUE(write_file_atomic(path, kPayload, sizeof(kPayload) - 1));
  sha1_hash(kPayload, sizeof(kPayload) - 1, digest);

  /* Right size, flipped digest bit: a stale cache for a newer build. */
  digest[0] ^= 0x01;
  TEST_ASSERT_TRUE(!bootstrap_cache_load(path, sizeof(kPayload) - 1, digest,
                                         &out));
  TEST_ASSERT_TRUE(out == NULL);

  TEST_ASSERT_TRUE(rmtree(dir));
  return 0;
}

static int test_commit_then_load_round_trip(void) {
  char dir[64];
  char path[128];
  uint8_t digest[SHA1_DIGEST_SIZE];
  uint8_t *out = NULL;

  TEST_ASSERT_EQ_INT(0, make_temp_dir(dir, sizeof(dir)));
  snprintf(path, sizeof(path), "%s/onionhen.elf", dir);

  TEST_ASSERT_TRUE(!bootstrap_cache_commit(NULL, kPayload, sizeof(kPayload)));
  TEST_ASSERT_TRUE(!bootstrap_cache_commit(path, NULL, sizeof(kPayload)));
  TEST_ASSERT_TRUE(!bootstrap_cache_commit(path, kPayload, 0));

  TEST_ASSERT_TRUE(
      bootstrap_cache_commit(path, kPayload, sizeof(kPayload) - 1));
  sha1_hash(kPayload, sizeof(kPayload) - 1, digest);

  TEST_ASSERT_TRUE(bootstrap_cache_load(path, sizeof(kPayload) - 1, digest,
                                        &out));
  TEST_ASSERT_TRUE(out != NULL);
  TEST_ASSERT_MEMEQ(kPayload, out, sizeof(kPayload) - 1);
  /* A hit hands back an owned copy, not a view into the cache file. */
  TEST_ASSERT_TRUE(out != (uint8_t *)kPayload);
  free(out);
  out = NULL;

  /* Re-committing replaces the previous cache in place. */
  const uint8_t kReplacement[] = "second";
  TEST_ASSERT_TRUE(
      bootstrap_cache_commit(path, kReplacement, sizeof(kReplacement) - 1));
  sha1_hash(kReplacement, sizeof(kReplacement) - 1, digest);
  TEST_ASSERT_TRUE(bootstrap_cache_load(path, sizeof(kReplacement) - 1, digest,
                                        &out));
  TEST_ASSERT_MEMEQ(kReplacement, out, sizeof(kReplacement) - 1);
  free(out);

  TEST_ASSERT_TRUE(rmtree(dir));
  return 0;
}

static int test_try_load_hit_and_miss(void) {
  char dir[64];
  char path[128];
  uint8_t digest[SHA1_DIGEST_SIZE];
  uint8_t *out = NULL;

  TEST_ASSERT_EQ_INT(0, make_temp_dir(dir, sizeof(dir)));
  snprintf(path, sizeof(path), "%s/onionhen.elf", dir);
  sha1_hash(kPayload, sizeof(kPayload) - 1, digest);

  /* No cache yet: a miss is a NULL return, not an error. */
  TEST_ASSERT_TRUE(bootstrap_cache_try_load(path, sizeof(kPayload) - 1,
                                            digest) == NULL);

  TEST_ASSERT_TRUE(
      bootstrap_cache_commit(path, kPayload, sizeof(kPayload) - 1));
  out = bootstrap_cache_try_load(path, sizeof(kPayload) - 1, digest);
  TEST_ASSERT_TRUE(out != NULL);
  TEST_ASSERT_MEMEQ(kPayload, out, sizeof(kPayload) - 1);
  free(out);

  /* A stale digest is still a miss. */
  digest[0] ^= 0x01;
  TEST_ASSERT_TRUE(bootstrap_cache_try_load(path, sizeof(kPayload) - 1,
                                            digest) == NULL);

  TEST_ASSERT_TRUE(rmtree(dir));
  return 0;
}

static int test_store_if_valid_size_gate(void) {
  char dir[64];
  char path[128];
  uint8_t digest[SHA1_DIGEST_SIZE];
  uint8_t *out = NULL;

  TEST_ASSERT_EQ_INT(0, make_temp_dir(dir, sizeof(dir)));
  snprintf(path, sizeof(path), "%s/onionhen.elf", dir);

  /* A decompressed size that disagrees with pack time must not be cached. */
  bootstrap_cache_store_if_valid(path, kPayload, sizeof(kPayload), 999);
  TEST_ASSERT_TRUE(!if_exists(path));

  /* Matching size writes it, and it loads back. */
  bootstrap_cache_store_if_valid(path, kPayload, sizeof(kPayload) - 1,
                                 sizeof(kPayload) - 1);
  TEST_ASSERT_TRUE(if_exists(path));

  sha1_hash(kPayload, sizeof(kPayload) - 1, digest);
  out = bootstrap_cache_try_load(path, sizeof(kPayload) - 1, digest);
  TEST_ASSERT_TRUE(out != NULL);
  TEST_ASSERT_MEMEQ(kPayload, out, sizeof(kPayload) - 1);
  free(out);

  TEST_ASSERT_TRUE(rmtree(dir));
  return 0;
}

int test_bootstrap_cache_suite(void) {
  int failures = 0;
  failures += onion_test_run("bootstrap_cache_load_bad_args",
                             test_load_rejects_bad_args);
  failures += onion_test_run("bootstrap_cache_load_missing_and_dir",
                             test_load_missing_and_not_a_file);
  failures += onion_test_run("bootstrap_cache_load_size_mismatch",
                             test_load_size_mismatch);
  failures += onion_test_run("bootstrap_cache_load_sha1_mismatch",
                             test_load_sha1_mismatch);
  failures += onion_test_run("bootstrap_cache_commit_round_trip",
                             test_commit_then_load_round_trip);
  failures += onion_test_run("bootstrap_cache_try_load_hit_and_miss",
                             test_try_load_hit_and_miss);
  failures += onion_test_run("bootstrap_cache_store_if_valid_size_gate",
                             test_store_if_valid_size_gate);
  return failures;
}
