cmake_minimum_required (VERSION 3.20)

set(basename "daemon")
project(${basename} C CXX ASM)

onion_module_prologue(C_STANDARD 11 CXX_STANDARD 20)

set(D_CWD "${CMAKE_CURRENT_SOURCE_DIR}")
onion_output_dirs()
set(D_SRC ${D_CWD}/source)

add_executable(${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}.elf")

# Finalize main target sources
target_compile_options(${PROJECT_NAME} PUBLIC
	$<$<COMPILE_LANGUAGE:C>:${C_DEFS} ${C_FLAGS}>
	$<$<COMPILE_LANGUAGE:CXX>:${CXX_DEFS} ${CXX_FLAGS}>
	$<$<COMPILE_LANGUAGE:ASM>:${ASM_FLAGS}>
)
target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--gc-sections" "-Wl,--icf=safe")

file(GLOB SrcFiles CONFIGURE_DEPENDS ${D_SRC}/*.c ${D_SRC}/*.cpp ${D_SRC}/../include/*.h ${D_SRC}/../include/*.hpp ${D_SRC}/*.s ${ONIONHEN_THIRD_PARTY_DIR}/cjson/*.cpp ${ONIONHEN_THIRD_PARTY_DIR}/cjson/*.hpp)

set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS} ")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-keyword-macro -fPIC -fPIE -march=znver2 -Wall -Werror -fstack-protector-all -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -gfull -gdwarf-2 -pedantic -pedantic-errors")
set(CMAKE_C_FLAGS_RELEASE "-Os -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG")

target_sources(${PROJECT_NAME} PRIVATE ${SrcFiles} "${ONIONHEN_COMMON_DIR}/elfldr_remote.c")
# .incbin in embedded_payloads.c is invisible to the compiler dependency scanner.
# Without OBJECT_DEPENDS, embedded payload rebuilds do not re-embed into daemon.elf.
if(DEFINED ONIONHEN_OUT_BIN)
	set(_onion_util_elf "${ONIONHEN_OUT_BIN}/util.elf")
	set(_onion_elfldr_elf "${ONIONHEN_OUT_BIN}/onion_elfldr.elf")
else()
	set(_onion_util_elf "${PROJECT_ROOT}/bin/util.elf")
	set(_onion_elfldr_elf "${PROJECT_ROOT}/bin/onion_elfldr.elf")
endif()
set_source_files_properties(
	${D_SRC}/embedded_payloads.c
	PROPERTIES OBJECT_DEPENDS
		"${ONIONHEN_OUT_BIN}/shellui.elf;${_onion_util_elf};${_onion_elfldr_elf}"
)
target_compile_definitions(${PROJECT_NAME} PRIVATE
		ONIONHEN_SHELLUI_ELF="${ONIONHEN_OUT_BIN}/shellui.elf"
		ONIONHEN_UTIL_ELF="${_onion_util_elf}"
		ONIONHEN_ELFLDR_ELF="${_onion_elfldr_elf}"
)
onion_target_debug_define(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE
	"${D_CWD}/include"
	"${ONIONHEN_THIRD_PARTY_DIR}/cjson"
	"${PROJECT_ROOT}/libonion_settings/include"
	"${PROJECT_ROOT}/libonion_ipc/include"
	"${PROJECT_ROOT}/libonion_proc/include"
	"${PROJECT_ROOT}/libonion_fps/include"
	"${PROJECT_ROOT}/libonion_sprx/include"
	"${PROJECT_ROOT}/libonion_platform/include"
	"${PROJECT_ROOT}/libonion_plugin_session/include"
	"${PROJECT_ROOT}/libonion_plugin_ui/include"
	"${PROJECT_ROOT}/libonion_plugin_bridge/include"
	"${PROJECT_ROOT}/libonion_plugin_manager/include"
	"${PROJECT_ROOT}/include"
)
target_link_directories	(${PROJECT_NAME} PUBLIC "${ONIONHEN_PS5_STUB_DIR}")
add_dependencies(${PROJECT_NAME} onion_version util onion_elfldr_server NineS shellui hijacker onion_settings onion_ipc onion_proc onion_fps onion_platform onion_ready onion_plugin_session onion_plugin_bridge onion_plugin_manager onion_elfldr onion_sprx)
# Supervises private :9020 via external :9021, then restarts embedded util via :9020.
target_link_libraries	(${PROJECT_NAME} PUBLIC onion_ipc onion_settings onion_proc onion_fps onion_platform onion_ready onion_plugin_session onion_plugin_bridge onion_plugin_manager onion_elfldr onion_sprx hijacker SceSystemService SceNotification SceUserService SceNetCtl SceSysCore kernel_sys NineS)

onion_strip_debug(${PROJECT_NAME})
