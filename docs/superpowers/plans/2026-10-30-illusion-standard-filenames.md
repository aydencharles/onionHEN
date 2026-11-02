# Illusion XML Standard Filename Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans or superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Load standard `<TITLE_ID>.xml` Illusion patch files while filtering metadata for the active game version.

**Architecture:** Keep generic filename parsing strict and special-case only exact title-ID `.xml` stems. Repository discovery admits that filename for the current version; loading passes the current `GameKey` to the XML parser, which already checks title and `AppVer`.

**Tech Stack:** C11 filename utilities, C++17 cheat repository/parser factory, Markdown documentation.

**Spec:** `docs/superpowers/specs/2026-10-30-illusion-standard-filenames-design.md`

## Global Constraints

- Do not relax non-XML filename matching.
- Reject XML title/version mismatches before patches are exposed.
- Do not claim console testing.

## Review Focus

- Standard title-only XML is matched only for the same title ID.
- Metadata still filters to the active version in both browse and runtime loading.
- Existing versioned XML and all other cheat formats retain current behavior.

---

### Task 1: Recognize and discover title-only XML

**Files:**
- Modify: `source/util/source/cheats/cheat_flatten.c`
- Modify: `source/util/source/cheats/cheat_repository.cpp`

- [x] Accept exact `<TITLE_ID>.xml` names with an empty version field; reject the same stem for other extensions and reject extra prefixes/suffixes.
- [x] Allow an empty version match only for XML candidates; preserve exact version matching for every other file.
- [x] Review diff and run `git diff --check`.

### Task 2: Supply the active game context to XML parsing

**Files:**
- Modify: `source/util/include/cheats/i_cheat_parser.hpp`
- Modify: `source/util/include/cheats/cheat_repository.hpp`
- Modify: `source/util/source/cheats/cheat_parser_factory.cpp`
- Modify: `source/util/source/cheats/cheat_repository.cpp`
- Modify: `source/util/source/cheats/cheat_service.cpp`

- [x] Thread the current title ID and normalized version from browse/runtime requests through repository loading.
- [x] Keep existing filename-derived context as a fallback for factory callers without explicit context.
- [x] Confirm the XML parser receives active context and retains its title and `AppVer` checks.
- [x] Review diff and run `git diff --check`.

### Task 3: Document supported filenames

**Files:**
- Modify: `source/util/source/cheats/README.md`
- Modify: `docs/util_arch/cheats_cpp.md`
- [x] Explain that standard `<TITLE_ID>.xml` is accepted and `AppVer` is checked at load time; keep version-specific filenames documented.
- [x] Review diff and report that build, tests, and PS5 validation were not run.
