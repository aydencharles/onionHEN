cmake_minimum_required(VERSION 3.20)

set(basename "onion_sprx")
project(${basename} CXX)

onion_module_prologue(CXX_STANDARD 20)

add_library(${PROJECT_NAME} STATIC
  source/sprx_loader.cpp
  source/sprx_catalog.cpp
  source/sprx_plugin_manager.cpp
  source/ptrace_sprx_runtime.cpp
)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")

target_compile_options(${PROJECT_NAME} PUBLIC
  $<$<COMPILE_LANGUAGE:CXX>:${CXX_DEFS} ${CXX_FLAGS}>
)

set(CMAKE_CXX_FLAGS "${ONION_PS5_TARGET_FLAGS} -gfull -gdwarf-2 -pedantic -pedantic-errors -Wall -Werror -fPIC -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG")

target_include_directories(${PROJECT_NAME}
  PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${PROJECT_ROOT}/libonion_elfldr/include"
          "${PROJECT_ROOT}/libonion_proc/include"
          "${PROJECT_ROOT}/libonion_platform/include"
          "${PROJECT_ROOT}/include"
)

add_dependencies(${PROJECT_NAME} onion_elfldr onion_proc)
target_link_libraries(${PROJECT_NAME} PUBLIC onion_elfldr onion_proc)
