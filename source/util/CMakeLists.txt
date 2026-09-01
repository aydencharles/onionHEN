cmake_minimum_required (VERSION 3.20)

set(basename "util")

project(${basename} C CXX ASM)

onion_module_prologue(C_STANDARD 11 CXX_STANDARD 20)

set(D_CWD "${CMAKE_CURRENT_SOURCE_DIR}")
onion_output_dirs()
set(D_SRC ${D_CWD}/source)

add_executable(${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "util.elf")

target_compile_options(${PROJECT_NAME} PUBLIC
	$<$<COMPILE_LANGUAGE:C>:${C_DEFS} ${C_FLAGS}>
	$<$<COMPILE_LANGUAGE:CXX>:${CXX_DEFS} ${CXX_FLAGS}>
	$<$<COMPILE_LANGUAGE:ASM>:${ASM_FLAGS}>
)

file(GLOB SrcFiles CONFIGURE_DEPENDS
  ${D_SRC}/*.c
  ${D_SRC}/*.cpp
  ${D_SRC}/cheats/*.c
  ${D_SRC}/cheats/*.cpp
  ${D_SRC}/cheats/sync/*.cpp
  ${ONIONHEN_THIRD_PARTY_DIR}/cheat_support/*.c
  ${D_SRC}/../include/*.h
  ${D_SRC}/../include/*.hpp
  ${D_SRC}/*.s
  ${ONIONHEN_THIRD_PARTY_DIR}/cjson/*.cpp
  ${ONIONHEN_THIRD_PARTY_DIR}/cjson/*.hpp
 
)

# ftpsrv is linked into util as a library-style module.  Its standalone main
# and platform-specific alternatives are intentionally excluded; the service
# facade owns the worker thread and lifecycle.
set(FTPSRV_SOURCE_DIR "${ONIONHEN_THIRD_PARTY_DIR}/ftpsrv")
set(FTPSRV_SOURCES
  "${FTPSRV_SOURCE_DIR}/srv.c"
  "${FTPSRV_SOURCE_DIR}/cmd.c"
  "${FTPSRV_SOURCE_DIR}/cmd-proc.c"
  "${FTPSRV_SOURCE_DIR}/cmd-comp.c"
  "${FTPSRV_SOURCE_DIR}/cmd-prospero.c"
  "${FTPSRV_SOURCE_DIR}/notify.c"
  "${FTPSRV_SOURCE_DIR}/io.c"
  "${FTPSRV_SOURCE_DIR}/io-freebsd.c"
  "${FTPSRV_SOURCE_DIR}/self.c"
  "${FTPSRV_SOURCE_DIR}/self-prospero.c"
)

# ShadowMountPlus is linked into util the same way as ftpsrv: a pinned
# third-party module whose standalone main is excluded; the service facade and
# shadowmount_main.cpp own the lifecycle.
set(SHADOWMOUNT_SOURCE_DIR "${ONIONHEN_THIRD_PARTY_DIR}/ShadowMountPlus")
set(SHADOWMOUNT_SOURCES
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_appdb.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_config_mount.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_fakelib.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_filesystem.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_gameinfo.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_game_cache.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_game_lifecycle.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_image.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_image_cache.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_install.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_install_queue.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_kstuff.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_log.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_manual.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_mdbg.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_mount_device.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_path_state.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_path_utils.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_scan.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_scan_tree.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_scanner.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_shellcore_flags.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_stability.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_time.c"
  "${SHADOWMOUNT_SOURCE_DIR}/src/sm_title_state.c"
)

# The PS5 payload SDK does not ship libsqlite3; the public-domain amalgamation
# is vendored and compiled like the rest of third_party.
set(ONION_SQLITE_SOURCES "${ONIONHEN_THIRD_PARTY_DIR}/sqlite/sqlite3.c")

# DPI package install server (network installer) linked into util the same way
# as ftpsrv: the vendored standalone's main was renamed to pkg_server_main and
# the service facade owns the worker thread and lifecycle.
set(PKGNET_SOURCE_DIR "${ONIONHEN_THIRD_PARTY_DIR}/pkgserver")
set(PKGNET_SOURCES
  "${PKGNET_SOURCE_DIR}/pkg_server.c"
  "${PKGNET_SOURCE_DIR}/multipart.c"
)

# Web UI bundle (built by scripts/build.sh before configure). Embedded into
# pkg_server.c via .incbin; rebuild the object when the bundle changes.
set(WEBUI_INDEX "${D_CWD}/../webui/dist/index.html")
if(NOT EXISTS "${WEBUI_INDEX}")
	message(FATAL_ERROR
		"WebUI bundle missing (looked in ${WEBUI_INDEX}). "
		"Run scripts/build.sh so the Vite step produces dist/index.html.")
endif()
set_source_files_properties("${PKGNET_SOURCE_DIR}/pkg_server.c" PROPERTIES
	COMPILE_DEFINITIONS "ONIONHEN_WEBUI_INDEX=\"${WEBUI_INDEX}\""
	OBJECT_DEPENDS "${WEBUI_INDEX}")

set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS} ")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-keyword-macro -fPIC -fPIE -march=znver2 -Wall -Werror -fstack-protector-all -Wno-unused-const-variable -Wno-deprecated-declarations -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -gfull -gdwarf-2 -pedantic -pedantic-errors")
set(CMAKE_C_FLAGS_RELEASE "-Os -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG")

target_sources(${PROJECT_NAME} PRIVATE ${SrcFiles} ${FTPSRV_SOURCES}
  ${SHADOWMOUNT_SOURCES} ${ONION_SQLITE_SOURCES} ${PKGNET_SOURCES}
  "${ONIONHEN_COMMON_DIR}/elfldr_remote.c")
onion_target_debug_define(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE
  "${D_CWD}/include"
  "${ONIONHEN_THIRD_PARTY_DIR}/cjson"
  # miniz / sha256 / mc4 headers live beside their vendored .c files.
  "${ONIONHEN_THIRD_PARTY_DIR}/cheat_support"
  "${ONIONHEN_THIRD_PARTY_DIR}/keystone/include"
  "${PROJECT_ROOT}/libhijacker/include"
  "${PROJECT_ROOT}/libonion_settings/include"
  "${PROJECT_ROOT}/libonion_ipc/include"
  "${PROJECT_ROOT}/libonion_proc/include"
  "${PROJECT_ROOT}/libonion_platform/include"
  "${PROJECT_ROOT}/libonion_payload/include"
  "${PROJECT_ROOT}/libonion_elfldr/include"
  "${PROJECT_ROOT}/include"
)
# CBC for MC4 / ShnExt AES
target_compile_definitions(${PROJECT_NAME} PRIVATE CBC=1)
set(ONION_CURL_ROOT "${PS5_PAYLOAD_SDK}/target/user/homebrew")
set(ONION_CURL_CA_BUNDLE "${ONION_CURL_ROOT}/etc/ca-bundle.crt")
if(NOT EXISTS "${ONION_CURL_ROOT}/include/curl/curl.h" OR
   NOT EXISTS "${ONION_CURL_ROOT}/lib/libcurl.a" OR
   NOT EXISTS "${ONION_CURL_CA_BUNDLE}")
	message(FATAL_ERROR
		"PS5 payload SDK libcurl and CA bundle are required for cheat catalog HTTP "
		"(looked in ${ONION_CURL_ROOT}).")
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE
	CURL_STATICLIB
	ONION_CURL_CA_BUNDLE="${ONION_CURL_CA_BUNDLE}"
  VERSION_TAG="OnionHEN ftpsrv module"
  SHADOWMOUNT_VERSION="OnionHEN shadowmount module"
  ONIONHEN_SHADOWMOUNT_TEMPLATE="${SHADOWMOUNT_SOURCE_DIR}/config.ini.example"
  ONIONHEN_SHADOWMOUNT_ICON="${SHADOWMOUNT_SOURCE_DIR}/smp_icon.png"
)
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
	"${ONION_CURL_ROOT}/include")
target_include_directories(${PROJECT_NAME} PRIVATE "${FTPSRV_SOURCE_DIR}")
target_include_directories(${PROJECT_NAME} PRIVATE "${PKGNET_SOURCE_DIR}"
  "${PKGNET_SOURCE_DIR}/include")
target_include_directories(${PROJECT_NAME} PRIVATE
  "${SHADOWMOUNT_SOURCE_DIR}"
  "${SHADOWMOUNT_SOURCE_DIR}/include"
  "${ONIONHEN_THIRD_PARTY_DIR}/sqlite")
# The CA bundle is embedded with .incbin, which is invisible to the compiler's
# normal dependency scanner.
set_source_files_properties(
	"${D_SRC}/cheats/sync/http_transport_ps5.cpp"
	PROPERTIES OBJECT_DEPENDS "${ONION_CURL_CA_BUNDLE}"
)
# Upstream ftpsrv is compiled with a less strict warning profile than the
# OnionHEN util sources.  Keep the shared target's -Werror for our code while
# allowing the pinned third-party module to retain its upstream implementation.
set_source_files_properties(${FTPSRV_SOURCES} PROPERTIES
  COMPILE_FLAGS "-Wno-error -Wno-unused-parameter -Wno-sign-compare")
set_source_files_properties(${PKGNET_SOURCES} PROPERTIES
  COMPILE_FLAGS "-Wno-error -Wno-unused-parameter -Wno-sign-compare")
set_source_files_properties(${SHADOWMOUNT_SOURCES} ${ONION_SQLITE_SOURCES}
  PROPERTIES COMPILE_FLAGS
  "-Wno-error -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function")
target_link_directories(${PROJECT_NAME} PUBLIC
  "${ONIONHEN_PS5_STUB_DIR}"
  "${ONIONHEN_THIRD_PARTY_DIR}/keystone/lib"
  "${ONION_CURL_ROOT}/lib"
  "${PS5_PAYLOAD_SDK}/target/lib"
)
target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--gc-sections" "-Wl,--icf=safe")
add_dependencies(${PROJECT_NAME} hijacker onion_settings onion_ipc onion_proc onion_platform onion_payload onion_ready onion_elfldr)
# keystone: ShnExt ASM only
target_link_libraries(${PROJECT_NAME} PUBLIC
    onion_ipc
    onion_settings
    onion_proc
    onion_platform
    onion_payload
    onion_ready
    onion_elfldr
    keystone
    hijacker
    SceSystemService
    SceUserService
    SceAppInstUtil
    SceNotification
    SceNetCtl
    SceNet
    kernel_sys
    curl
    psl
    ssl
    crypto
    zstd
    z
)
set_source_files_properties(
  ${ONIONHEN_THIRD_PARTY_DIR}/cheat_support/aes.c
  ${ONIONHEN_THIRD_PARTY_DIR}/cheat_support/base64.c
  ${ONIONHEN_THIRD_PARTY_DIR}/cheat_support/miniz.c
  ${ONIONHEN_THIRD_PARTY_DIR}/cheat_support/sha256.c
  PROPERTIES COMPILE_FLAGS "-Wno-error -Wno-unused-function -Wno-sign-compare -Wno-unused-parameter"
)
# miniz.h ships static-inline helpers that trip PS5's global -Werror in C++.
set_source_files_properties(
  ${D_SRC}/cheats/sync/zip_archive.cpp
  PROPERTIES COMPILE_FLAGS "-Wno-error -Wno-unused-function -Wno-sign-compare -Wno-unused-parameter"
)
file(GLOB CHEAT_ENGINE_SRCS CONFIGURE_DEPENDS ${D_SRC}/cheats/*.c)
set_source_files_properties(${CHEAT_ENGINE_SRCS} PROPERTIES
  COMPILE_FLAGS "-Wno-error -Wno-unused-parameter -Wno-sign-compare"
)

onion_strip_debug(${PROJECT_NAME})
