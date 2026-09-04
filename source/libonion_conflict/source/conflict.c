/* Copyright (C) 2026 OnionHEN / LightningMods */

#include <onion/conflict.h>

#include <stddef.h>

#if !defined(ONION_HOST_TEST)
#include <onion/proc_query.h>
#endif

static const char *kEtaHenNames[] = {
    "etaHEN.elf",
    NULL,
};

static const OnionConflictStrategy kStrategies[] = {
    {"etaHEN", kEtaHenNames},
};

const OnionConflictStrategy *onion_conflict_strategies(size_t *out_count) {
  if (out_count)
    *out_count = sizeof(kStrategies) / sizeof(kStrategies[0]);
  return kStrategies;
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
      if (find_pid(*name) > 0)
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
