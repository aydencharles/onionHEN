###################################################################################################
# OnionHEN user Payload helpers (strict private :9020 + exact PID files)
###################################################################################################

cmake_minimum_required(VERSION 3.20)

set(basename "onion_payload")
project(${basename} C)

onion_module_prologue(C_STANDARD 11)
set(D_CWD "${PROJECT_SOURCE_DIR}")

add_library(${PROJECT_NAME} STATIC source/payload.c source/payload_config.c)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")

set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS_MARCH} -fPIC -fstack-protector-all -O2 -Wall -Wextra -Werror")

target_include_directories(${PROJECT_NAME}
  PUBLIC  "${D_CWD}/include"
  PRIVATE
    "${ONIONHEN_THIRD_PARTY_DIR}/cjson"
    "${D_CWD}/../include"
    "${D_CWD}/../libonion_proc/include"
    "${D_CWD}/../libonion_platform/include"
)

target_link_libraries(${PROJECT_NAME} PUBLIC onion_proc onion_platform)
add_dependencies(${PROJECT_NAME} onion_proc onion_platform)
