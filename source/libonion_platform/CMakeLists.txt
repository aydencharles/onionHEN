###################################################################################################
# OnionHEN platform leaves: fs / log / net / notify
###################################################################################################

cmake_minimum_required(VERSION 3.20)

set(basename "onion_platform")
project(${basename} C)

onion_module_prologue(C_STANDARD 11)
set(D_CWD "${PROJECT_SOURCE_DIR}")

set(ONION_CJSON_SOURCE "${ONIONHEN_THIRD_PARTY_DIR}/cjson/cJSON.cpp")
set_source_files_properties("${ONION_CJSON_SOURCE}" PROPERTIES
  LANGUAGE C
  COMPILE_OPTIONS "-Wno-unreachable-code-generic-assoc"
)

add_library(${PROJECT_NAME} STATIC
  source/fs.c
  source/tree.c
  source/log.c
  source/net.c
  source/notify.c
  source/notify_i18n.c
  "${ONION_CJSON_SOURCE}"
)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(I18N_DIR "${D_CWD}/../i18n")
set(I18N_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(NOTIFY_I18N_CATALOG "${I18N_GENERATED_DIR}/notify_i18n_catalog.inc")
file(GLOB I18N_LOCALE_JSON CONFIGURE_DEPENDS "${I18N_DIR}/*.json")
add_custom_command(
  OUTPUT "${NOTIFY_I18N_CATALOG}"
  COMMAND "${Python3_EXECUTABLE}" "${I18N_DIR}/generate_catalog.py"
    --locales-dir "${I18N_DIR}"
    --catalog notifications
    --output "${NOTIFY_I18N_CATALOG}"
  DEPENDS
    "${I18N_DIR}/generate_catalog.py"
    ${I18N_LOCALE_JSON}
  COMMENT "Generating notification i18n catalog"
  VERBATIM
)
add_custom_target(onion_platform_i18n_catalog DEPENDS "${NOTIFY_I18N_CATALOG}")
add_dependencies(${PROJECT_NAME} onion_platform_i18n_catalog)

set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS_MARCH} -fPIC -fstack-protector-all -O2 -Wall -Wextra -Werror")

target_include_directories(${PROJECT_NAME}
  PUBLIC  "${D_CWD}/include"
  PRIVATE "${D_CWD}/../include" "${I18N_GENERATED_DIR}"
          "${ONIONHEN_THIRD_PARTY_DIR}/cjson"
)
