/* Copyright (C) 2026 OnionHEN / LightningMods */

#include <onion/conflict.h>
#include <onion/elf_name.h>
#include <onion/proc_query.h>

#include <stddef.h>
#include <string.h>

/* elfldr_spawn names from etaHEN bootstrapper; not daemon.elf / util.elf. */
static const char *kEtaHenNames[] = {
    "etaHEN Utility Daemon",
    "etaHEN Critical services",
    NULL,
};

static const char *kYoncoreNames[] = {
    "Yoncore.elf",
    NULL,
};

static const char *kKylinCoreNames[] = {
    "kylin-core.elf",
    NULL,
};

static const char *kWmdwJwmNames[] = {
    "wmdw-jwm.elf",
    NULL,
};

static const char *kCheatRunnerNames[] = {
    "CheatRunner.elf",
    NULL,
};

static const OnionConflictStrategy kStrategies[] = {
    {"etaHEN", kEtaHenNames},
    {"Yoncore", kYoncoreNames},
    {"kylin-core", kKylinCoreNames},
    {"wmdw-jwm", kWmdwJwmNames},
    {"CheatRunner", kCheatRunnerNames},
};

const OnionConflictStrategy *onion_conflict_strategies(size_t *out_count) {
  if (out_count)
    *out_count = sizeof(kStrategies) / sizeof(kStrategies[0]);
  return kStrategies;
}

enum { kNameAltMax = 64 };
enum { kCandMax = 8 };

static int add_unique_name(const char **list, size_t *count, size_t cap,
                           const char *name) {
  if (!name || !name[0] || *count >= cap)
    return 0;
  for (size_t i = 0; i < *count; ++i) {
    if (strcmp(list[i], name) == 0)
      return 0;
  }
  list[(*count)++] = name;
  return 1;
}

/* Live ki_comm may keep or drop ".elf", and is truncated to COMMLEN. */
static int conflict_pid_live(onion_conflict_find_pid_fn find_pid,
                             const char *name) {
  if (!name || !name[0])
    return 0;

  const size_t n = strlen(name);
  const size_t stem_n = onion_elf_name_stem_n(name, n);
  if (stem_n == 0)
    return find_pid(name) > 0;
  if (stem_n >= (size_t)kNameAltMax)
    return find_pid(name) > 0;

  char stem[kNameAltMax];
  char with_elf[kNameAltMax];
  char truncs[3][kNameAltMax];
  memcpy(stem, name, stem_n);
  stem[stem_n] = '\0';

  int have_elf = 0;
  if (stem_n + (size_t)ONION_ELF_SUFFIX_LEN < (size_t)kNameAltMax) {
    memcpy(with_elf, stem, stem_n);
    memcpy(with_elf + stem_n, ONION_ELF_SUFFIX, (size_t)ONION_ELF_SUFFIX_LEN + 1);
    have_elf = 1;
  }

  const char *cands[kCandMax];
  size_t nc = 0;
  add_unique_name(cands, &nc, kCandMax, name);
  add_unique_name(cands, &nc, kCandMax, stem);
  if (have_elf)
    add_unique_name(cands, &nc, kCandMax, with_elf);

  const size_t base_n = nc;
  size_t nt = 0;
  for (size_t i = 0; i < base_n && nt < 3; ++i) {
    const size_t ln = strlen(cands[i]);
    if (ln <= (size_t)ONION_PROC_KI_COMM_LEN)
      continue;
    memcpy(truncs[nt], cands[i], (size_t)ONION_PROC_KI_COMM_LEN);
    truncs[nt][ONION_PROC_KI_COMM_LEN] = '\0';
    add_unique_name(cands, &nc, kCandMax, truncs[nt]);
    ++nt;
  }

  for (size_t i = 0; i < nc; ++i) {
    if (find_pid(cands[i]) > 0)
      return 1;
  }
  return 0;
}

const char *onion_conflict_scan(const OnionConflictStrategy *strategies,
                                size_t count,
                                onion_conflict_find_pid_fn find_pid) {
  if (!strategies || !find_pid || count == 0)
    return NULL;

  for (size_t i = 0; i < count; ++i) {
    const OnionConflictStrategy *strategy = &strategies[i];
    if (!strategy->family || !strategy->family[0] || !strategy->proc_names)
      continue;

    for (const char *const *name = strategy->proc_names; *name; ++name) {
      if (!(*name)[0])
        continue;
      if (conflict_pid_live(find_pid, *name))
        return strategy->family;
    }
  }
  return NULL;
}

const char *onion_conflict_detect_with(onion_conflict_find_pid_fn find_pid) {
  size_t count = 0;
  const OnionConflictStrategy *strategies = onion_conflict_strategies(&count);
  return onion_conflict_scan(strategies, count, find_pid);
}

const char *onion_conflict_detect(void) {
#if defined(ONION_HOST_TEST)
  return NULL;
#else
  return onion_conflict_detect_with(onion_find_pid);
#endif
}
