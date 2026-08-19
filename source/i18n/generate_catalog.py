#!/usr/bin/env python3
"""Generate compile-time translation tables from locale JSON files.

Discovers source/i18n/*.json (or explicit --zh-cn/--en-us plus extras).
Each file may include:
  meta.id        locale id stored in config.ini (en, zh-Hans, ar, …)
  meta.bcp47     filename-style tag
  meta.fallback  exactly one locale must be true (English)
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any


SECTIONS = ("toolbox", "notifications")
PRINTF_CONVERSION = re.compile(
    r"%(?:\d+\$)?[-+ #0']*(?:\*|\d+)?(?:\.(?:\*|\d+))?"
    r"(?:hh|h|ll|l|j|z|t|L)?([diuoxXfFeEgGaAcspn%])"
)
DUPLICATED_INTERNAL_SPACE = re.compile(r"\S {2,}\S")

REQUIRED_TOKENS = {
    "toolbox": {
        "group.payloads.sub": ("Kstuff", "ShadowMount+"),
        "ftp.autoload": ("FTP",),
    },
    "notifications": {
        "notify.kstuff.loading": ("Kstuff",),
        "notify.kstuff.load_failed": ("Kstuff",),
        "notify.kstuff.load_elfldr_failed": ("Kstuff",),
        "notify.kstuff.deleted": ("Kstuff",),
        "notify.shadowmount.scan_started": ("ShadowMount+",),
        "notify.shadowmount.launch_failed": ("ShadowMount+",),
        "notify.shadowmount.external_removed": ("ShadowMount+",),
        "notify.shadowmount.remove_failed": ("ShadowMount+",),
        "notify.shadowmount.next_boot_on": ("ShadowMount+",),
        "notify.shadowmount.next_boot_off": ("ShadowMount+",),
    },
}

FILENAME_IDS = {
    "en-US.json": ("en", True),
    "zh-CN.json": ("zh-Hans", False),
    "zh-TW.json": ("zh-Hant", False),
    "ja-JP.json": ("ja", False),
    "ko-KR.json": ("ko", False),
    "fr-FR.json": ("fr", False),
    "de-DE.json": ("de", False),
    "it-IT.json": ("it", False),
    "es-ES.json": ("es", False),
    "pt-BR.json": ("pt-BR", False),
    "pl-PL.json": ("pl", False),
    "ru-RU.json": ("ru", False),
    "ar-SA.json": ("ar", False),
    "th-TH.json": ("th", False),
}


def printf_conversions(value: str) -> list[str]:
    return [item for item in PRINTF_CONVERSION.findall(value) if item != "%"]


def cpp_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def load_locale(path: Path) -> dict[str, Any]:
    try:
        data: Any = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"error: cannot load {path}: {error}") from error

    if not isinstance(data, dict):
        raise SystemExit(f"error: {path} must be a JSON object")

    extra = set(data) - set(SECTIONS) - {"meta"}
    if extra:
        raise SystemExit(f"error: {path} has unknown sections: {sorted(extra)}")
    missing = [section for section in SECTIONS if section not in data]
    if missing:
        raise SystemExit(f"error: {path} missing sections: {missing}")

    inferred_id, inferred_fallback = FILENAME_IDS.get(path.name, (path.stem, False))
    meta = data.get("meta", {})
    if meta is None:
        meta = {}
    if not isinstance(meta, dict):
        raise SystemExit(f"error: {path} meta must be an object")
    locale_id = meta.get("id") or inferred_id
    fallback = bool(meta.get("fallback", inferred_fallback))
    if not isinstance(locale_id, str) or not locale_id:
        raise SystemExit(f"error: {path} meta.id is required")

    for section in SECTIONS:
        values = data[section]
        if not isinstance(values, dict) or not values:
            raise SystemExit(
                f"error: {path} section {section!r} must be a non-empty object"
            )
        invalid = [
            key
            for key, value in values.items()
            if not isinstance(key, str)
            or not key
            or not isinstance(value, str)
            or "\0" in value
        ]
        if invalid:
            raise SystemExit(
                f"error: {path} section {section!r} has invalid entries: "
                f"{invalid}"
            )

    return {
        "path": path,
        "id": locale_id,
        "bcp47": str(meta.get("bcp47") or path.stem),
        "fallback": fallback,
        "toolbox": data["toolbox"],
        "notifications": data["notifications"],
    }


def discover_locales(locales_dir: Path | None, explicit: list[Path]) -> list[dict[str, Any]]:
    paths = list(explicit)
    if locales_dir is not None:
        paths.extend(sorted(locales_dir.glob("*.json")))
    # Preserve first occurrence so explicit flags still win.
    seen: set[Path] = set()
    unique: list[Path] = []
    for path in paths:
        resolved = path.resolve()
        if resolved in seen:
            continue
        seen.add(resolved)
        unique.append(path)
    if not unique:
        raise SystemExit("error: no locale JSON files found")
    locales = [load_locale(path) for path in unique]
    ids = [locale["id"] for locale in locales]
    if len(ids) != len(set(ids)):
        raise SystemExit(f"error: duplicate locale ids: {ids}")
    fallbacks = [locale for locale in locales if locale["fallback"]]
    if len(fallbacks) != 1:
        raise SystemExit("error: exactly one locale must set meta.fallback=true")
    fallback = fallbacks[0]
    rest = sorted(
        (locale for locale in locales if locale is not fallback),
        key=lambda locale: locale["id"],
    )
    return [fallback, *rest]


def validate_locales(locales: list[dict[str, Any]]) -> None:
    fallback = locales[0]
    for section in SECTIONS:
        expected = set(fallback[section])
        for locale in locales[1:]:
            keys = set(locale[section])
            if keys == expected:
                continue
            details = []
            missing = sorted(expected - keys)
            extra = sorted(keys - expected)
            if missing:
                details.append(f"missing: {', '.join(missing)}")
            if extra:
                details.append(f"extra: {', '.join(extra)}")
            raise SystemExit(
                f"error: locale key mismatch in {section} for "
                f"{locale['path'].name}; " + "; ".join(details)
            )
        for key, fallback_text in fallback[section].items():
            expected_conv = printf_conversions(fallback_text)
            for locale in locales[1:]:
                got = printf_conversions(locale[section][key])
                if got != expected_conv:
                    raise SystemExit(
                        f"error: printf conversions differ for {section} "
                        f"{key!r} in {locale['path'].name}: "
                        f"{got} != {expected_conv}"
                    )

    for locale in locales:
        for section, required_keys in REQUIRED_TOKENS.items():
            for key, tokens in required_keys.items():
                value = locale[section].get(key)
                if value is None:
                    raise SystemExit(
                        f"error: required consistency key {section}.{key} "
                        f"is missing in {locale['path'].name}"
                    )
                missing = [token for token in tokens if token not in value]
                if missing:
                    raise SystemExit(
                        f"error: {section}.{key} in {locale['path'].name} "
                        f"is missing required token(s): {', '.join(missing)}"
                    )
        for section in SECTIONS:
            for key, value in locale[section].items():
                if DUPLICATED_INTERNAL_SPACE.search(value):
                    raise SystemExit(
                        f"error: duplicated internal space in {section}.{key} "
                        f"in {locale['path'].name}"
                    )


def generate(catalog: str, locales: list[dict[str, Any]]) -> str:
    ids = [locale["id"] for locale in locales]
    lines = [
        "// Generated from source/i18n locale catalogs. Do not edit.",
        f"#define I18N_LOCALE_COUNT {len(locales)}",
        "static const char *const kI18nLocaleIds[I18N_LOCALE_COUNT] = {",
        "    " + ", ".join(cpp_string(locale_id) for locale_id in ids) + ",",
        "};",
        "static const int kI18nLocaleFallback = 0;",
        "",
    ]
    if catalog == "toolbox":
        lines.extend(
            [
                "struct Entry {",
                "  const char *key;",
                "  const char *text[I18N_LOCALE_COUNT];",
                "};",
                "constexpr Entry kTable[] = {",
            ]
        )
        section = "toolbox"
    else:
        lines.extend(
            [
                "typedef struct {",
                "  const char *key;",
                "  const char *text[I18N_LOCALE_COUNT];",
                "} notify_translation_t;",
                "static const notify_translation_t kTranslations[] = {",
            ]
        )
        section = "notifications"
    for key in locales[0][section]:
        texts = ", ".join(cpp_string(locale[section][key]) for locale in locales)
        lines.append(f"  {{{cpp_string(key)}, {{{texts}}}}},")
    lines.extend(["};", ""])
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--locales-dir", type=Path)
    parser.add_argument("--zh-cn", type=Path)
    parser.add_argument("--en-us", type=Path)
    parser.add_argument(
        "--catalog", required=True, choices=("toolbox", "notifications")
    )
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    explicit = [path for path in (args.en_us, args.zh_cn) if path]
    locales = discover_locales(args.locales_dir, explicit)
    validate_locales(locales)
    content = generate(args.catalog, locales)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.output.exists() and args.output.read_text(encoding="utf-8") == content:
        return
    args.output.write_text(content, encoding="utf-8")


if __name__ == "__main__":
    main()
