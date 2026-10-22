# Standalone FPS counter + on-screen overlay.
#
# Builds, in one configure and without the daemon/util/kstuff/bootstrapper:
#   shellui.elf   the overlay-only ShellUI module (ONIONHEN_FPS_OVERLAY_ONLY)
#   fps_counter.elf  the payload: safe scanout sampler + injector that embeds
#                    shellui.elf and injects it into SceShellUI.
# All code is reused from ../source so it stays in sync with the fork.
cmake_minimum_required(VERSION 3.20)
project(fps_counter C CXX ASM)

if(NOT DEFINED ENV{PS5_PAYLOAD_SDK} AND NOT DEFINED PS5_PAYLOAD_SDK)
  message(FATAL_ERROR "Set PS5_PAYLOAD_SDK (path to ps5-payload-sdk)")
endif()
if(NOT DEFINED PS5_PAYLOAD_SDK)
  set(PS5_PAYLOAD_SDK "$ENV{PS5_PAYLOAD_SDK}")
endif()

# Variables the repo's modules expect from the superbuild.
get_filename_component(ONIONHEN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(PROJECT_ROOT "${ONIONHEN_ROOT}/source")
set(ONIONHEN_THIRD_PARTY_DIR "${ONIONHEN_ROOT}/third_party")
set(ONIONHEN_COMMON_DIR "${PROJECT_ROOT}/common")
set(ONIONHEN_PS5_STUB_DIR "${PROJECT_ROOT}/platform/ps5/stubs")
list(APPEND CMAKE_MODULE_PATH "${PROJECT_ROOT}/cmake")
include(OnionTarget)

set(ONIONHEN_OUT_LIB "${CMAKE_BINARY_DIR}/lib")
set(ONIONHEN_OUT_BIN "${CMAKE_BINARY_DIR}/bin")
file(MAKE_DIRECTORY "${ONIONHEN_OUT_LIB}" "${ONIONHEN_OUT_BIN}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${ONIONHEN_OUT_BIN}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${ONIONHEN_OUT_LIB}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${ONIONHEN_OUT_LIB}")

set(ONIONHEN_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated/include")
set(ONIONHEN_VERSION_HEADER "${ONIONHEN_GENERATED_INCLUDE_DIR}/onion/version.h")
add_custom_target(onion_version
  COMMAND "${CMAKE_COMMAND}"
    "-DREPO_ROOT=${ONIONHEN_ROOT}"
    "-DOUTPUT_FILE=${ONIONHEN_VERSION_HEADER}"
    -P "${PROJECT_ROOT}/cmake/GenerateVersionHeader.cmake"
  BYPRODUCTS "${ONIONHEN_VERSION_HEADER}"
  COMMENT "Generating build version"
  VERBATIM)

include_directories(BEFORE "${ONIONHEN_GENERATED_INCLUDE_DIR}")
include_directories("${PROJECT_ROOT}/include")
include_directories(SYSTEM "${PS5_PAYLOAD_SDK}" "${PS5_PAYLOAD_SDK}/include")
link_directories("${ONIONHEN_OUT_LIB}" "${ONIONHEN_PS5_STUB_DIR}")

# ShellUI module in overlay-only mode (see source/shellui/src/prx.cpp).
set(ONIONHEN_FPS_OVERLAY_ONLY ON CACHE BOOL "" FORCE)

# Libraries shellui and the injector need (same order as source/CMakeLists.txt).
foreach(lib libonion_proc libonion_platform libonion_payload libonion_ipc
            libonion_settings libonion_detour libonion_ready
            libonion_plugin_ui libonion_plugin_bridge libonion_elfldr libNineS)
  add_subdirectory("${PROJECT_ROOT}/${lib}" "${lib}")
endforeach()
add_subdirectory("${PROJECT_ROOT}/shellui" shellui)

# ---------------------------------------------------------------------------
# fps_counter.elf
# ---------------------------------------------------------------------------
set(FPS_FLAGS "${ONION_PS5_TARGET_FLAGS_MARCH} -fPIC -fPIE -fstack-protector-all")
set(CMAKE_C_FLAGS "${FPS_FLAGS} -Wall -Werror -Wno-keyword-macro")
set(CMAKE_CXX_FLAGS "${FPS_FLAGS} -O2 -Wall -Wextra -Werror -fno-exceptions")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 11)

set(FPS_LIB "${PROJECT_ROOT}/libonion_fps")

add_executable(fps_counter
  src/main.cpp
  src/embedded_overlay.c
  "${FPS_LIB}/source/fps_formula.cpp"
  "${FPS_LIB}/source/fps_dce.cpp"
  "${FPS_LIB}/source/fps_publish.cpp"
)
set_target_properties(fps_counter PROPERTIES OUTPUT_NAME "fps_counter.elf")

# shim/ first so main.cpp's daemon_ops.hpp / globalconf.hpp / <onion/settings.hpp>
# resolve to the small stand-ins.
target_include_directories(fps_counter BEFORE PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/shim")
target_include_directories(fps_counter PRIVATE
  "${FPS_LIB}/include"
  "${PROJECT_ROOT}/libonion_proc/include"
  "${PROJECT_ROOT}/libonion_platform/include"
  "${PROJECT_ROOT}/libNineS/include"   # freebsd-helper.h (proc_query)
)

# Embed the freshly built overlay module.
target_compile_definitions(fps_counter PRIVATE
  FPS_OVERLAY_ELF="${ONIONHEN_OUT_BIN}/shellui.elf")
set_source_files_properties(src/embedded_overlay.c PROPERTIES
  OBJECT_DEPENDS "${ONIONHEN_OUT_BIN}/shellui.elf")
add_dependencies(fps_counter shellui)

target_link_options(fps_counter PRIVATE -Wl,--gc-sections)
target_link_libraries(fps_counter PRIVATE
  NineS onion_elfldr onion_proc onion_ready onion_platform
  SceSystemService kernel_sys)
onion_strip_debug(fps_counter)
