# Illusion XML Standard Filename Support

## Intent

Allow users to install an unmodified PS-Game-Patch XML file named `<TITLE_ID>.xml` from the upstream Illusion patch collection. OnionHEN must still apply only metadata whose `AppVer` matches the running game, so a filename without a version can never widen patch eligibility.

## Current behavior and evidence

- The upstream `illusionyy/PS-Game-Patch` README names distributed patch files `(TitleID).xml` and documents `AppVer` inside metadata.
- OnionHEN discovery currently accepts only `TITLE_ID_VERSION` filenames, and the XML parser obtains both its target title and version from that filename.
- This mismatch means standard files are skipped before parsing. The parser already checks the XML `TitleID` list and metadata `AppVer` against its target context.

## Approaches

1. Keep requiring versioned filenames and ask users to rename/duplicate upstream files. This keeps the repository API unchanged but makes the feature less compatible with its standard source.
2. Accept exact title-only `.xml` filenames and pass the active `GameKey` into XML loading. This keeps other formats and filename rules intact while using the existing title/version metadata checks. **Chosen** because it supports the unmodified upstream file safely.
3. Parse every version from one XML file without game context. Rejected because direct-address metadata could be exposed to the wrong game version.

## Design

- Recognize `<TITLE_ID>.xml` only when the stem is exactly a title ID; do not relax JSON, SHN, MC4, or ShnExt filename parsing.
- Let repository discovery match title-only XML for the current title and version. Continue to require `AppVer` matching in the parser; `AppVer="mask"` keeps its existing behavior.
- Pass the active `GameKey` to file loading and use it as XML target context. Versioned files continue to work with the same context.
- Document both standard `<TITLE_ID>.xml` and existing `<TITLE_ID>_<VERSION>.xml` layouts.

## Constraints and non-goals

- No changes to patch interpretation or memory application.
- No automatic downloading or rewriting of XML files.
- No console behavior is claimed without a PS5 test.
- Keep the existing per-game version match as a required gate.
