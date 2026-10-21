# Standalone FPS counter: one ELF, no daemon/util/kstuff/settings/ShellUI.
# Reuses libhijacker, libonion_fps sources, proc_dmap/proc_query and log.c
# straight from ../source so the sampler stays in sync with the fork.
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
set(ONIONHEN_PS5_STUB_DIR "${PROJECT_ROOT}/platform/ps5/stubs")
list(APPEND CMAKE_MODULE_PATH "${PROJECT_ROOT}/cmake")
include(OnionTarget)

set(ONIONHEN_OUT_LIB "${CMAKE_BINARY_DIR}/lib")
set(ONIONHEN_OUT_BIN "${CMAKE_BINARY_DIR}/bin")
file(MAKE_DIRECTORY "${ONIONHEN_OUT_LIB}" "${ONIONHEN_OUT_BIN}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${ONIONHEN_OUT_BIN}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${ONIONHEN_OUT_LIB}")

include_directories("${PROJECT_ROOT}/include")
include_directories(SYSTEM "${PS5_PAYLOAD_SDK}" "${PS5_PAYLOAD_SDK}/include")
link_directories("${ONIONHEN_OUT_LIB}" "${ONIONHEN_PS5_STUB_DIR}")

# Only third-party lib the sampler needs is hijacker (PS4 BC hook).
add_subdirectory("${PROJECT_ROOT}/libhijacker" hijacker)

set(FPS_FLAGS "${ONION_PS5_TARGET_FLAGS_MARCH} -fPIC -fPIE -fstack-protector-all")
set(CMAKE_C_FLAGS "${FPS_FLAGS} -Wall -Werror -Wno-keyword-macro")
set(CMAKE_CXX_FLAGS "${FPS_FLAGS} -O2 -Wall -Wextra -Werror -fno-exceptions")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 11)

set(FPS_LIB "${PROJECT_ROOT}/libonion_fps")
set(PROC_LIB "${PROJECT_ROOT}/libonion_proc")
set(PLAT_LIB "${PROJECT_ROOT}/libonion_platform")

add_executable(fps_counter
  src/main.cpp
  "${PROJECT_ROOT}/daemon/source/daemon_fps.cpp"
  "${FPS_LIB}/source/fps_formula.cpp"
  "${FPS_LIB}/source/fps_dce.cpp"
  "${FPS_LIB}/source/fps_agc.cpp"
  "${FPS_LIB}/source/fps_publish.cpp"
  "${FPS_LIB}/source/fps_bc.cpp"
  "${PROJECT_ROOT}/libonion_detour/source/hde64.cpp"
  "${PROC_LIB}/source/proc_dmap.cpp"
  "${PROC_LIB}/source/proc_query.cpp"
  "${PLAT_LIB}/source/log.c"
)
set_target_properties(fps_counter PROPERTIES OUTPUT_NAME "fps_counter.elf")

# shim/ first so daemon_fps.cpp's daemon_ops.hpp / globalconf.hpp /
# <onion/settings.hpp> resolve to the small stand-ins instead of the daemon's.
target_include_directories(fps_counter BEFORE PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/shim")
target_include_directories(fps_counter PRIVATE
  "${FPS_LIB}/include"
  "${PROC_LIB}/include"
  "${PLAT_LIB}/include"
  "${PROJECT_ROOT}/libhijacker/include"
  "${PROJECT_ROOT}/libonion_detour/include"
  "${PROJECT_ROOT}/libNineS/include"   # freebsd-helper.h (proc_query)
)
target_link_options(fps_counter PRIVATE -Wl,--gc-sections)
target_link_libraries(fps_counter PRIVATE hijacker
  SceSystemService kernel_sys)
onion_strip_debug(fps_counter)
