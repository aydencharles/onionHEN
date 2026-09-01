#!/usr/bin/env bash
# OnionHEN one-shot build pipeline
#
# Phases:
#   1) configure (prospero-cmake / PS5 payload SDK)
#   2) build libs + shellui
#   3) stage the kstuff dependency; ftpsrv is compiled into util from source
#   4) build daemon + util
#   5) build bootstrapper  (-> bin/bootstrapper.elf + .lzma)
#   6) build unpacker / OnionHEN.elf   (embeds bootstrapper.elf.lzma)
#
# Usage:
#   export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
#   ./scripts/build.sh              # cleans previous outputs first
#   ./scripts/build.sh --clean      # same as default; kept for compatibility
#   ./scripts/build.sh --release    # non-interactive Release build
#   ./scripts/build.sh --jobs 8
#   ./scripts/build.sh --stub-missing   # compile-only placeholders for missing external ELFs
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE="${ROOT}/source"
# CMake binary dir + final ELFs/libs: <repo>/build/{bin,lib}/
BUILD="${BUILD_DIR:-${ROOT}/build}"
CACHE="${ONIONHEN_CACHE_DIR:-${ROOT}/.cache/dependencies}"
BIN="${BUILD}/bin"

PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-${PS5SDK:-}}"
DEFAULT_BUILD_TYPE="Debug"
BUILD_TYPE="${BUILD_TYPE:-}"
BUILD_TYPE_EXPLICIT=0
[[ -n "${BUILD_TYPE}" ]] && BUILD_TYPE_EXPLICIT=1
JOBS="${JOBS:-}"
CONFIGURE_ONLY=0
STUB_MISSING=0
SKIP_UNPACKER=0
SKIP_DEPENDENCY_SYNC=0
FORCE_DEPENDENCY_SYNC=0
INIT_SUBMODULES=0

# Auto job count
if [[ -z "${JOBS}" ]]; then
  if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
  elif command -v sysctl >/dev/null 2>&1; then
    JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
  else
    JOBS=4
  fi
fi

log()  { printf '\n\033[1;34m==>\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m[ok]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[warn]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[error]\033[0m %s\n' "$*" >&2; exit 1; }

usage() {
  cat <<EOF
OnionHEN build pipeline

Usage: $(basename "$0") [options]

Options:
  --clean              Accepted for compatibility; builds are cleaned by default
  --configure-only     Only run CMake configure
  --jobs <n>           Parallel build jobs (default: ${JOBS})
  --build-type <t>     Debug|Release (default: ${DEFAULT_BUILD_TYPE})
  --debug              Same as --build-type Debug
  --release            Same as --build-type Release
  --build-dir <path>   CMake binary dir (default: <repo>/build)
  --cache-dir <path>   Download cache directory (default: <repo>/.cache/dependencies)
  --stub-missing       Create tiny placeholder ELFs if external blobs are missing
                       (links, but NOT for real hardware)
  --skip-unpacker      Stop after bootstrapper (no OnionHEN.elf unpacker)
  --skip-dependency-sync
                       Do not call scripts/sync_dependencies.sh
  --force-dependency-sync
                       Re-download kstuff even if already cached
  --init-submodules    git submodule update --init before sync
  -h, --help           This help

Environment:
  PS5_PAYLOAD_SDK   Path to ps5-payload-sdk (required)
  ONIONHEN_CACHE_DIR Override dependency cache directory
  BUILD_DIR         Override build directory
  BUILD_TYPE        Debug|Release; skips the interactive build-type prompt

Third-party (pinned source under third_party/ + release fallbacks):
  See third_party/README.md and scripts/sync_dependencies.sh

  kstuff.elf              <- EchoStretch/kstuff-lite
  ftpsrv                  <- drakmor/ftpsrv source, compiled into util

  External elfldr @ 9021 is required for initial bootstrap but is not vendored.
  OnionHEN embeds its private runtime loader as onion_elfldr.elf @ 9020.

  Removed: external elfldr.elf (9021), ps5debug, app-dumper, Byepervisor/hen, Discord RPC

Built-in outputs (under <repo>/build/):
  build/bin/*.elf           final ELFs (util, daemon, bootstrapper, OnionHEN, …)
  build/lib/*.a             first-party static libs
  build/bin/shellui.elf     daemon embed input
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --clean) shift ;;
    --configure-only) CONFIGURE_ONLY=1; shift ;;
    --jobs) JOBS="$2"; shift 2 ;;
    --build-type) BUILD_TYPE="$2"; BUILD_TYPE_EXPLICIT=1; shift 2 ;;
    --debug) BUILD_TYPE="Debug"; BUILD_TYPE_EXPLICIT=1; shift ;;
    --release) BUILD_TYPE="Release"; BUILD_TYPE_EXPLICIT=1; shift ;;
    --build-dir) BUILD="$2"; shift 2 ;;
    --cache-dir) CACHE="$2"; shift 2 ;;
    --stub-missing) STUB_MISSING=1; shift ;;
    --skip-unpacker) SKIP_UNPACKER=1; shift ;;
    --skip-dependency-sync) SKIP_DEPENDENCY_SYNC=1; shift ;;
    --force-dependency-sync) FORCE_DEPENDENCY_SYNC=1; shift ;;
    --init-submodules) INIT_SUBMODULES=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "Unknown option: $1 (see --help)" ;;
  esac
done

normalize_build_type() {
  case "$1" in
    [Dd][Ee][Bb][Uu][Gg]|[Dd]) printf 'Debug' ;;
    [Rr][Ee][Ll][Ee][Aa][Ss][Ee]|[Rr]) printf 'Release' ;;
    *) return 1 ;;
  esac
}

select_build_type() {
  local selected="${BUILD_TYPE:-}"

  if [[ "${BUILD_TYPE_EXPLICIT}" -eq 1 ]]; then
    BUILD_TYPE="$(normalize_build_type "${selected}")" ||
      die "invalid build type: ${selected} (expected Debug or Release)"
    return 0
  fi

  if [[ -t 0 && -t 1 ]]; then
    printf '\nSelect build type:\n'
    printf '  1) Debug (default)\n'
    printf '  2) Release\n'
    printf 'Build type [1]: '
    read -r selected || selected=""
    case "${selected}" in
      ""|1|[Dd]|[Dd][Ee][Bb][Uu][Gg]) BUILD_TYPE="Debug" ;;
      2|[Rr]|[Rr][Ee][Ll][Ee][Aa][Ss][Ee]) BUILD_TYPE="Release" ;;
      *)
        warn "unknown build type selection '${selected}', using ${DEFAULT_BUILD_TYPE}"
        BUILD_TYPE="${DEFAULT_BUILD_TYPE}"
        ;;
    esac
  else
    BUILD_TYPE="${DEFAULT_BUILD_TYPE}"
  fi
}

select_build_type

# ---------------------------------------------------------------------------
# Preconditions
# ---------------------------------------------------------------------------
[[ -n "${PS5_PAYLOAD_SDK}" ]] || die "PS5_PAYLOAD_SDK is not set"
[[ -d "${PS5_PAYLOAD_SDK}" ]] || die "PS5_PAYLOAD_SDK not a directory: ${PS5_PAYLOAD_SDK}"
[[ -x "${PS5_PAYLOAD_SDK}/bin/prospero-cmake" ]] || die "prospero-cmake not found under SDK"
[[ -d "${SOURCE}" ]] || die "source/ missing at ${SOURCE}"

export PS5_PAYLOAD_SDK
export PATH="${PS5_PAYLOAD_SDK}/bin:${PATH}"
export LLVM_CONFIG="${LLVM_CONFIG:-/opt/homebrew/opt/llvm@18/bin/llvm-config}"

need_cmd() { command -v "$1" >/dev/null 2>&1 || die "missing command: $1"; }
need_cmd cmake
need_cmd ninja
need_cmd python3
# lzma or xz required later for bootstrapper packing
if ! command -v lzma >/dev/null 2>&1 && ! command -v xz >/dev/null 2>&1; then
  die "need lzma or xz (e.g. brew install xz)"
fi

#
# prospero-clang++ needs target libc++ / libc++abi / libunwind in the SDK.
# Incomplete binary installs often only have C stubs; CMake then dies with
#   ld.lld: error: unable to find library -lc++
# Official fix: build SDK from source + ./libcxx.sh, or re-install a full
# release zip that includes target/lib/libc++.a (v0.41+).
#
ensure_sdk_libcxx() {
  local libdir="${PS5_PAYLOAD_SDK}/target/lib"
  local need=0
  for f in libc++.a libc++abi.a libunwind.a; do
    [[ -f "${libdir}/${f}" ]] || need=1
  done
  [[ -d "${PS5_PAYLOAD_SDK}/target/include/c++/v1" ]] || need=1

  if [[ "${need}" -eq 0 ]]; then
    ok "SDK C++ runtime present (libc++ / libc++abi / libunwind)"
    return 0
  fi

  warn "SDK missing C++ runtime under ${libdir}"
  warn "Attempting to fetch from ps5-payload-dev/sdk latest release zip…"

  need_cmd curl
  need_cmd unzip

  local tmp
  tmp="$(mktemp -d "${TMPDIR:-/tmp}/onion-sdk-cxx.XXXXXX")"
  # shellcheck disable=SC2064
  trap "rm -rf '${tmp}'" RETURN

  local zip_url="https://github.com/ps5-payload-dev/sdk/releases/latest/download/ps5-payload-sdk.zip"
  if ! curl -fsSL --retry 3 -o "${tmp}/sdk.zip" "${zip_url}"; then
    die "download failed: ${zip_url}
Install a full SDK, or from SDK source tree run:
  sudo make DESTDIR=${PS5_PAYLOAD_SDK} install
  sudo -E ./libcxx.sh"
  fi

  unzip -q -o "${tmp}/sdk.zip" -d "${tmp}/out"
  local root
  root="$(find "${tmp}/out" -maxdepth 2 -type d -name 'ps5-payload-sdk' | head -1)"
  [[ -n "${root}" ]] || root="${tmp}/out/ps5-payload-sdk"
  [[ -d "${root}/target/lib" ]] || die "unexpected SDK zip layout"

  mkdir -p "${libdir}" "${PS5_PAYLOAD_SDK}/target/include"
  for f in libc++.a libc++abi.a libunwind.a libc++experimental.a; do
    if [[ -f "${root}/target/lib/${f}" ]]; then
      cp -f "${root}/target/lib/${f}" "${libdir}/${f}"
    fi
  done
  if [[ -d "${root}/target/include/c++" ]]; then
    rm -rf "${PS5_PAYLOAD_SDK}/target/include/c++"
    cp -a "${root}/target/include/c++" "${PS5_PAYLOAD_SDK}/target/include/"
  fi

  for f in libc++.a libc++abi.a libunwind.a; do
    [[ -f "${libdir}/${f}" ]] || die "still missing ${libdir}/${f} after merge"
  done
  ok "merged C++ runtime into ${PS5_PAYLOAD_SDK}"
}

CMAKE=("${PS5_PAYLOAD_SDK}/bin/prospero-cmake")

build_webui() {
  local webui_dir="${ROOT}/source/webui"
  [[ -f "${webui_dir}/package.json" ]] || die "WebUI package.json missing: ${webui_dir}"

  log "Building WebUI (Vite single-file bundle)"
  (cd "${webui_dir}" && npm ci && npm run build)
  [[ -f "${webui_dir}/dist/index.html" ]] ||
    die "WebUI build did not produce ${webui_dir}/dist/index.html"
  ok "WebUI ready -> ${webui_dir}/dist/index.html"
}

# ---------------------------------------------------------------------------
# Dependency source builds and fallback staging
# ---------------------------------------------------------------------------
stage_dependencies() {
  if [[ "${SKIP_DEPENDENCY_SYNC}" -eq 1 ]]; then
    warn "skipping dependency sync (--skip-dependency-sync)"
    return 0
  fi

  log "Syncing third-party embeds (submodules / GitHub releases)"
  local args=()
  [[ "${STUB_MISSING}" -eq 1 ]] && args+=(--stub-missing)
  [[ "${FORCE_DEPENDENCY_SYNC}" -eq 1 ]] && args+=(--force)
  [[ "${INIT_SUBMODULES}" -eq 1 ]] && args+=(--init-submodules)

  if [[ ! -x "${ROOT}/scripts/sync_dependencies.sh" ]]; then
    die "missing ${ROOT}/scripts/sync_dependencies.sh"
  fi
  ONIONHEN_CACHE_DIR="${CACHE}" \
    "${ROOT}/scripts/sync_dependencies.sh" "${args[@]+"${args[@]}"}"
}

# ---------------------------------------------------------------------------
# Configure
# ---------------------------------------------------------------------------
clean_build_artifacts() {
  log "Cleaning previous build outputs"

  case "${BUILD}" in
    ""|"/"|"/Users"|"/Users/${USER:-}"|"${ROOT}"|"${SOURCE}")
      die "refusing to clean unsafe build dir: ${BUILD}"
      ;;
  esac

  rm -rf "${BUILD}"

  # Legacy in-tree bin (real dir or symlink).
  rm -rf "${SOURCE}/bin"
  # Legacy in-tree cmake tree
  rm -rf "${SOURCE}/build"

  ok "old build outputs removed"
}

configure() {
  log "Configure (${BUILD_TYPE})"
  mkdir -p "${BUILD}"

  "${CMAKE[@]}" \
    -S "${SOURCE}" \
    -B "${BUILD}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DONIONHEN_KSTUFF_ELF="${CACHE}/kstuff.elf" \
    -DPS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK}"
  ok "configured -> ${BUILD}"
}

build_targets() {
  local targets=("$@")
  log "Build: ${targets[*]}  (-j${JOBS})"
  cmake --build "${BUILD}" -j"${JOBS}" --target "${targets[@]}"
}

# ---------------------------------------------------------------------------
# Main pipeline
# ---------------------------------------------------------------------------
main() {
  log "OnionHEN build"
  echo "  ROOT     = ${ROOT}"
  echo "  SDK      = ${PS5_PAYLOAD_SDK}"
  echo "  BUILD    = ${BUILD}"
  echo "  CACHE    = ${CACHE}"
  echo "  TYPE     = ${BUILD_TYPE}"
  echo "  JOBS     = ${JOBS}"

  clean_build_artifacts
  ensure_sdk_libcxx
  build_webui
  stage_dependencies
  configure

  if [[ "${CONFIGURE_ONLY}" -eq 1 ]]; then
    ok "configure-only done"
    exit 0
  fi

  # Phase 1 — libraries + injectables (all outputs stay under build/bin)
  log "Phase 1/5: libraries + shellui"
  build_targets \
    NidResolver \
    hijacker \
    NineS \
    shellui

  # shellui.elf is a daemon embed input.
  if [[ -f "${BIN}/shellui.elf" ]]; then
    ok "built shellui.elf"
  else
    die "expected ${BIN}/shellui.elf after phase 1"
  fi

  # Phase 2 — daemons
  log "Phase 2/5: util + daemon"
  build_targets util daemon

  for f in daemon.elf util.elf; do
    [[ -f "${BIN}/${f}" ]] || die "missing ${BIN}/${f} after phase 2"
    ok "built ${f}"
  done

  # Phase 3 — bootstrapper (embeds daemon/util + kstuff + assets)
  log "Phase 3/5: bootstrapper"
  build_targets bootstrapper bootstrapper_packed

  # CMake declares bootstrapper.elf.lzma as an output and compresses it from
  # bootstrapper.elf. If the output is unavailable, retain the manual fallback
  # for older build trees.
  # and writes bootstrapper.elf.lzma.size. If lzma replaced the elf, restore
  # naming expected by unpacker.
  if [[ -f "${BIN}/bootstrapper.elf.lzma" ]]; then
    ok "bootstrapper.elf.lzma ready"
  elif [[ -f "${BIN}/bootstrapper.elf" ]]; then
    warn "lzma not produced by CMake output rule; packing manually"
    local elf="${BIN}/bootstrapper.elf"
    if stat -f%z "${elf}" >/dev/null 2>&1; then
      stat -f%z "${elf}" > "${elf}.lzma.size"
    else
      stat -c%s "${elf}" > "${elf}.lzma.size"
    fi
    if command -v lzma >/dev/null 2>&1; then
      # lzma -9 replaces file with .lzma suffix on some implementations
      cp -f "${elf}" "${elf}.bak"
      lzma -f -9 -k "${elf}" 2>/dev/null || lzma -f -9 "${elf}"
      if [[ -f "${elf}.lzma" ]]; then
        :
      elif [[ -f "${elf}" ]] && file "${elf}" | grep -qi lzma; then
        mv "${elf}" "${elf}.lzma"
        mv "${elf}.bak" "${elf}"
      fi
      [[ -f "${elf}.bak" ]] && mv -f "${elf}.bak" "${elf}" 2>/dev/null || true
    else
      xz -F lzma -f -9 -k -c "${elf}" > "${elf}.lzma"
    fi
    [[ -f "${BIN}/bootstrapper.elf.lzma" ]] || die "failed to produce bootstrapper.elf.lzma"
    ok "manual lzma pack done"
  else
    die "bootstrapper.elf missing in ${BIN}"
  fi

  if [[ ! -f "${BIN}/bootstrapper.elf.lzma.size" ]]; then
    # size file must be original decompressed size (ascii or raw — upstream uses stat text)
    if [[ -f "${BIN}/bootstrapper.elf" ]]; then
      if stat -f%z "${BIN}/bootstrapper.elf" >/dev/null 2>&1; then
        stat -f%z "${BIN}/bootstrapper.elf" > "${BIN}/bootstrapper.elf.lzma.size"
      else
        stat -c%s "${BIN}/bootstrapper.elf" > "${BIN}/bootstrapper.elf.lzma.size"
      fi
    fi
  fi

  if [[ "${SKIP_UNPACKER}" -eq 1 ]]; then
    log "Skip unpacker (--skip-unpacker)"
  else
    # Phase 5 — final payload (OnionHEN.elf embeds lzma bootstrapper)
    log "Phase 5/5: unpacker (OnionHEN.elf)"
    # Target project name is OnionHEN (see unpacker/CMakeLists.txt)
    if cmake --build "${BUILD}" -j"${JOBS}" --target OnionHEN 2>/dev/null; then
      ok "OnionHEN target built"
    else
      build_targets unpacker 2>/dev/null || build_targets OnionHEN
    fi
    if [[ -f "${BIN}/OnionHEN.elf" ]]; then
      ok "final payload: ${BIN}/OnionHEN.elf"
    else
      warn "OnionHEN.elf not found under bin/ — check unpacker target name/output"
      ls -la "${BIN}" || true
    fi
  fi

  log "Build finished"
  echo
  echo "Artifacts in ${BIN}:"
  ls -lah "${BIN}" 2>/dev/null || true
  echo
  ok "done"
}

main
