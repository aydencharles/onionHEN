###################################################################################################
# Foreign payload conflict detection (process-name strategies)
###################################################################################################

cmake_minimum_required(VERSION 3.20)

set(basename "onion_conflict")
project(${basename} C)

onion_module_prologue(C_STANDARD 11)
set(D_CWD "${PROJECT_SOURCE_DIR}")

add_library(${PROJECT_NAME} STATIC source/conflict.c)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")

set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS_MARCH} -fPIC -fstack-protector-all -O2 -Wall -Wextra -Werror")

target_include_directories(${PROJECT_NAME}
  PUBLIC  "${D_CWD}/include"
  PRIVATE
    "${D_CWD}/../include"
    "${D_CWD}/../libonion_proc/include"
)

target_link_libraries(${PROJECT_NAME} PUBLIC onion_proc)
add_dependencies(${PROJECT_NAME} onion_proc)
