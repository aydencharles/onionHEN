# Shared translations

User-facing text lives in one JSON file per locale and is compiled into the
ELF. The console does not load locale files at runtime.

Current locales: `zh-CN.json` (简体中文), `zh-TW.json` (繁體中文),
`en-US.json` (English), `ja-JP.json` (日本語), `ko-KR.json` (한국어),
`fr-FR.json` (Français), `de-DE.json` (Deutsch), `it-IT.json` (Italiano),
`es-ES.json` (Español), `pt-BR.json` (Português do Brasil),
`pl-PL.json` (Polski), `ru-RU.json` (Русский), `ar-SA.json`
(العربية, trial), and `th-TH.json` (ไทย).
The generator scans every `*.json` in this directory. Each locale is
selectable in Toolbox and via `toolbox.language`. The Settings XML is
still LTR, so Arabic glyphs render but the page layout is not mirrored.

## File layout

Each locale file has exactly two sections:

| Section | Keys | Runtime |
|---|---|---|
| `toolbox` | dotted ids such as `fan.enable` | `toolbox_i18n::tr("fan.enable")` |
| `notifications` | `notify.*` ids such as `notify.fan.open_failed` | `onion_notify(true, "notify.fan.open_failed")` |

`en-US.json` is the fallback locale (`meta.fallback: true`). Every other
locale file must contain the same keys and the same `printf` conversions.

The build runs `generate_catalog.py` and rejects:

- missing keys in any locale
- empty keys or embedded NUL
- mismatched `printf` conversions (`%s`, `%d`, `%i`, `%u`, `%X`, …)

Generated tables are embedded in `shellui` (toolbox) and
`libonion_platform` (notifications).

## How to add a Toolbox string

1. Add the same key to **every** locale JSON file under `toolbox`.
2. Prefer one complete sentence with `printf` placeholders when values are
   inserted. Do not split a sentence across several keys and concatenate
   them in C++ (word order cannot be translated).

   ```json
   "cheats.enable_fmt": "Enable/disable %s for %s"
   ```

   ```json
   "cheats.enable_fmt": "为 %s 启用/禁用 %s"
   ```

3. Format with the shared helper, not a local `snprintf` wrapper:

   ```cpp
   toolbox_i18n::format("cheats.enable_fmt", game, cheat);
   ```
4. Rebuild. Missing the key in one locale fails the catalog step.
5. Switch Toolbox language, leave the page, and reopen it. XML is built
   when the page opens. The same save path used by log level
   (`settings_commit` → `BREW_RELOAD_SETTINGS` → `LoadSettings`) tells
   daemon and util to apply the new language. That is a config reread,
   not a SceShellUI inject.

Missing toolbox keys render as the key itself (visible in the menu).

## How to add a notification

1. Add a stable `notify.<area>.<name>` key to **every** locale JSON file
   under `notifications`. Never use the English sentence as the key.
2. Keep `printf` placeholders identical in every language.
3. Pass the key, not the English text:

   ```c
   onion_notify(true, "notify.fan.open_failed");
   onion_notify(true, "notify.payload.loading", name);
   onion_notify_rich("notify.brand", "notify.boot.starting", ...);
   ```

4. Do not pass a raw English format string. Unknown keys are shown
   unchanged (English-looking leftover, or a debug `"%s"` passthrough).

ShellUI's `notify("…")` helper forwards to the same catalog. Unpacker
has its own `notify()` and does **not** use this catalog.

## How to add a language

Catalog compilation already scans this directory. A new JSON file is
embedded automatically. To make the language selectable you still need
the runtime mapping:

1. Add `<locale>.json` with the same keys as `en-US.json`, plus
   `meta.id` (the config.ini value), `meta.bcp47`, and
   `meta.fallback: false`.
2. Add `lang.<id>` to every locale file (the Toolbox list label).
3. Add a `kUiLanguage*` value, parse/serialize it in settings, and add
   a Toolbox list item with that integer.
4. Add the matching `Lang` / `ONION_NOTIFY_LANG_*` values and map
   `meta.id` in `notify_i18n.c` / `toolbox_i18n.cpp`.
5. If the PS5 system language should pick it when Toolbox language is
   `system`, add that id in `onion_notify_resolve_language`
   (`0` → `ja`, `2`/`22` → `fr`, `3`/`20` → `es`, `4` → `de`, `5` → `it`,
   `7`/`17` → `pt-BR`, `8` → `ru`, `9` → `ko`, `10` → `zh-Hant`,
    `11` → `zh-Hans`, `16` → `pl`, `21` → `ar`, `27` → `th`;
    everything else is `en`).

## Web UI

The browser pkg-installer UI in [`../webui/`](../webui/) keeps its own
dictionaries in `src/i18n.js` — the same 14-locale set as the Toolbox, but
bundled into the pkg-server's single-file page. Its language resolves the
same way as the Toolbox (`toolbox.language`, else the console system
language) and updates live when the daemon's system-language poll pushes
`BREW_UTIL_SET_SYSTEM_LANG` to util.

## Intentional exclusions

These user-visible strings are **not** in the JSON catalogs on purpose:

| Surface | Why |
|---|---|
| Settings menu label `★OnionHEN Tools` | Equal-length binary patch of `★Debug Settings`. Length is fixed. |
| HomeUI top-nav `OnionHEN` | Brand token, same in every language. |
| Notification watermark `[OnionHEN]` | Brand prefix in `onion_notify_format`. |
| About names, handles, Ko-fi URL, project URLs | Proper nouns / addresses. |
| Unpacker start-failure toasts | First-stage loader; no language setting and its own `notify()`. English only until it is wired to `onion_notify`. |
| Cheat names / descriptions from cheat files | Come from the cheat JSON, not OnionHEN. |

Logs (`LOG_*`) are developer-facing and stay English.

## Current coverage

Checked against call sites (not just the JSON files):

- Toolbox menus, game-options cheat entry, PKG `GetString` hooks, and
  About donor/WeChat labels go through `toolbox_i18n::tr()`.
- Daemon / util / shellui / bootstrapper toasts go through `notify.*`
  keys. Host tests cover zh-Hans, zh-Hant, en, ja, ko, fr, de, it, es,
  pt-BR, pl, ru, ar, and th lookup.
- Four toolbox keys are unused leftovers from an older menu grouping:
  `group.lang`, `group.lang.sub`, `group.shortcuts`, `group.shortcuts.sub`.
  They are translated but not shown.
- Welcome toast still concatenates `version + notify.boot.made_by + author`.
  That word order is correct for current zh and en; the other locales use
  the same concatenation.
- ShadowMount+ and PkgNet toolbox keys (`shadowmount.*`, `pkgnet.*`) and
  notification keys (`notify.shadowmount.*`, `notify.pkgnet.*`) are
  translated across all 14 locales.
- The Web UI (`source/webui/src/i18n.js`) carries its own dictionaries for
  the same 14 locales; its language resolves identically to the Toolbox and
  updates live on `BREW_UTIL_SET_SYSTEM_LANG`.

Arabic is a complete key-for-key trial locale, not a native RTL layout.
