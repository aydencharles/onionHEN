/* Shared ".elf" filename stem. "foo.elf" → stem "foo"; "foo" unchanged. */
#pragma once

#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ONION_ELF_SUFFIX ".elf"
#define ONION_ELF_SUFFIX_LEN 4

static inline int onion_elf_name_has_suffix(const char *name, size_t n) {
  return name != NULL && n >= (size_t)ONION_ELF_SUFFIX_LEN &&
         memcmp(name + n - (size_t)ONION_ELF_SUFFIX_LEN, ONION_ELF_SUFFIX,
                (size_t)ONION_ELF_SUFFIX_LEN) == 0;
}

/** Length of @name with one trailing ".elf" removed. Bare ".elf" → 0. */
static inline size_t onion_elf_name_stem_n(const char *name, size_t n) {
  if (onion_elf_name_has_suffix(name, n))
    return n - (size_t)ONION_ELF_SUFFIX_LEN;
  return n;
}

#ifdef __cplusplus
}
#endif
