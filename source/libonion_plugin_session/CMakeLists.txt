cmake_minimum_required(VERSION 3.20)

set(basename "onion_plugin_session")
project(${basename} CXX)

onion_module_prologue(CXX_STANDARD 20)

add_library(${PROJECT_NAME} STATIC source/plugin_session.cpp)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")
set(CMAKE_CXX_FLAGS "${ONION_PS5_TARGET_FLAGS_MARCH} -fPIC -fstack-protector-all -O2 -Wall -Wextra -Werror -fno-exceptions")
target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${PROJECT_NAME} PUBLIC onion_plugin_ui)
