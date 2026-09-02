cmake_minimum_required (VERSION 3.20)

set(basename "shellui")
project(${basename} C CXX ASM)

onion_module_prologue(C_STANDARD 11 CXX_STANDARD 20)

set(D_CWD "${CMAKE_CURRENT_SOURCE_DIR}")
onion_output_dirs()
set(D_SRC ${D_CWD}/src)

add_executable(${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}.elf")

# Finalize main target sources
target_compile_options(${PROJECT_NAME} PUBLIC
	$<$<COMPILE_LANGUAGE:C>:${C_DEFS} ${C_FLAGS}>
	$<$<COMPILE_LANGUAGE:CXX>:${CXX_DEFS} ${CXX_FLAGS}>
	$<$<COMPILE_LANGUAGE:ASM>:${ASM_FLAGS}>
)
target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--gc-sections" "-Wl,--icf=safe")

# A/B: disable HomeUI top-nav Hermes patch without editing sources.
#   cmake -DONIONHEN_SHELLUI_HOMEUI_TOP_NAV_PATCH=OFF ...
option(ONIONHEN_SHELLUI_HOMEUI_TOP_NAV_PATCH
	"Patch HomeUI top-nav Fps slot into OnionHEN entry" ON)
if(NOT ONIONHEN_SHELLUI_HOMEUI_TOP_NAV_PATCH)
	target_compile_definitions(${PROJECT_NAME} PRIVATE SHELLUI_HOMEUI_TOP_NAV_PATCH=0)
	message(STATUS "shellui: HomeUI top-nav patch DISABLED")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(I18N_DIR "${D_CWD}/../i18n")
set(I18N_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(I18N_CATALOG "${I18N_GENERATED_DIR}/toolbox_i18n_catalog.inc")
file(GLOB I18N_LOCALE_JSON CONFIGURE_DEPENDS "${I18N_DIR}/*.json")
add_custom_command(
	OUTPUT "${I18N_CATALOG}"
	COMMAND "${Python3_EXECUTABLE}" "${I18N_DIR}/generate_catalog.py"
		--locales-dir "${I18N_DIR}"
		--catalog toolbox
		--output "${I18N_CATALOG}"
	DEPENDS
		"${I18N_DIR}/generate_catalog.py"
		${I18N_LOCALE_JSON}
	COMMENT "Generating toolbox i18n catalog"
	VERBATIM
)
add_custom_target(shellui_i18n_catalog DEPENDS "${I18N_CATALOG}")

file(GLOB SrcFiles CONFIGURE_DEPENDS ${D_SRC}/*.c ${D_SRC}/*.cpp ${D_SRC}/../include/*.h ${D_SRC}/../include/*.hpp ${D_SRC}/*.s ${ONIONHEN_THIRD_PARTY_DIR}/cjson/*.cpp ${ONIONHEN_THIRD_PARTY_DIR}/cjson/*.hpp)

set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS} ")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-nested-anon-types -Wno-gnu-anonymous-struct -Wno-zero-length-array -Wno-keyword-macro -fPIC -fPIE -march=znver2 -Wall -Werror -fstack-protector-all -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -gfull -gdwarf-2 -pedantic -pedantic-errors")
set(CMAKE_C_FLAGS_RELEASE "-Os -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG")

target_sources(${PROJECT_NAME} PRIVATE ${SrcFiles})
add_dependencies(${PROJECT_NAME} shellui_i18n_catalog onion_version onion_ipc onion_settings onion_detour onion_proc onion_platform onion_ready onion_plugin_ui onion_plugin_bridge)
onion_target_debug_define(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE
	"${D_CWD}/include"
	"${I18N_GENERATED_DIR}"
	"${ONIONHEN_THIRD_PARTY_DIR}/cjson"
	"${ONIONHEN_THIRD_PARTY_DIR}/sha1"
	"${PROJECT_ROOT}/libonion_ipc/include"
	"${PROJECT_ROOT}/libonion_settings/include"
	"${PROJECT_ROOT}/libonion_detour/include"
	"${PROJECT_ROOT}/libonion_proc/include"
	"${PROJECT_ROOT}/libonion_platform/include"
	"${PROJECT_ROOT}/libonion_fps/include"
	"${PROJECT_ROOT}/libonion_plugin_ui/include"
	"${PROJECT_ROOT}/libonion_plugin_bridge/include"
	"${PROJECT_ROOT}/include"
)
target_link_directories	(${PROJECT_NAME} PUBLIC "${ONIONHEN_PS5_STUB_DIR}")
target_link_libraries	(${PROJECT_NAME} PUBLIC onion_detour onion_proc onion_platform onion_ipc onion_settings onion_ready onion_plugin_ui onion_plugin_bridge SceRegMgr SceSystemService SceUserService SceNetCtl)

# Add post-build command
onion_strip_debug(${PROJECT_NAME})
