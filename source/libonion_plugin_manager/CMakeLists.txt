cmake_minimum_required(VERSION 3.20)

set(basename "onion_plugin_manager")
project(${basename} CXX)

onion_module_prologue(CXX_STANDARD 20)
set(D_CWD "${PROJECT_SOURCE_DIR}")

add_library(${PROJECT_NAME} STATIC
  source/plugin_descriptor.cpp
  source/plugin_manager.cpp
)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")
set(CMAKE_CXX_FLAGS "${ONION_PS5_TARGET_FLAGS_MARCH} -fPIC -fstack-protector-all -O2 -Wall -Wextra -Werror")

target_include_directories(${PROJECT_NAME} PUBLIC "${D_CWD}/include")
