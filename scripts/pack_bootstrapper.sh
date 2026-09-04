#!/usr/bin/env bash
# Called from bootstrapper CMake POST_BUILD with TARGET_ELF set.
set -euo pipefail

elf="${TARGET_ELF:?TARGET_ELF not set}"
[[ -f "${elf}" ]] || { echo "missing ${elf}" >&2; exit 1; }

# Original (decompressed) size for unpacker
if stat -f%z "${elf}" >/dev/null 2>&1; then
  stat -f%z "${elf}" > "${elf}.lzma.size"
else
  stat -c%s "${elf}" > "${elf}.lzma.size"
fi

# SHA-1 of the uncompressed ELF (not the .lzma). Unpacker embeds this and
# uses it to decide whether /user/data/OnionHEN/onionhen.elf is still valid.
write_sha1() {
  local src="$1"
  local dest="$2"
  local hash=""
  if command -v shasum >/dev/null 2>&1; then
    hash="$(shasum -a 1 "${src}" | awk '{print $1}')"
  elif command -v sha1sum >/dev/null 2>&1; then
    hash="$(sha1sum "${src}" | awk '{print $1}')"
  elif command -v openssl >/dev/null 2>&1; then
    hash="$(openssl dgst -sha1 "${src}" | awk '{print $NF}')"
  else
    echo "shasum, sha1sum, or openssl required" >&2
    exit 1
  fi
  hash="$(printf '%s' "${hash}" | tr 'A-F' 'a-f')"
  if [[ ! "${hash}" =~ ^[0-9a-f]{40}$ ]]; then
    echo "invalid sha1 for ${src}: ${hash}" >&2
    exit 1
  fi
  printf '%s\n' "${hash}" > "${dest}"
}

write_sha1 "${elf}" "${elf}.sha1"

# Keep a copy — some lzma tools replace/remove the input
cp -f "${elf}" "${elf}.pre-lzma"

if command -v lzma >/dev/null 2>&1; then
  # Prefer writing alongside without deleting the original if -k is supported
  if lzma -h 2>&1 | grep -q -- ' -k'; then
    lzma -f -9 -k "${elf}"
  else
    # May rename elf -> elf.lzma
    lzma -f -9 "${elf}" || true
  fi
elif command -v xz >/dev/null 2>&1; then
  xz -F lzma -f -9 -c "${elf}.pre-lzma" > "${elf}.lzma"
else
  echo "lzma or xz required" >&2
  exit 1
fi

# Normalize to ${elf}.lzma and restore ${elf}
if [[ -f "${elf}.lzma" ]]; then
  :
elif [[ -f "${elf}" ]] && ! cmp -s "${elf}" "${elf}.pre-lzma" 2>/dev/null; then
  # Input was replaced in-place with compressed data under same name
  mv "${elf}" "${elf}.lzma"
fi

if [[ ! -f "${elf}.lzma" ]]; then
  echo "failed to produce ${elf}.lzma" >&2
  exit 1
fi

# Always leave uncompressed elf present for debugging / re-pack
mv -f "${elf}.pre-lzma" "${elf}"

echo "packed: ${elf}.lzma (size: ${elf}.lzma.size, sha1: ${elf}.sha1)"
