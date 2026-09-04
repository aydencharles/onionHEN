cmake_minimum_required (VERSION 3.20)

set(basename "unpacker")
project(${basename} C CXX ASM)

onion_module_prologue(C_STANDARD 11 CXX_STANDARD 20)

set(D_CWD "${CMAKE_CURRENT_SOURCE_DIR}")
onion_output_dirs()
set(D_SRC ${D_CWD}/source)

set(PROJECT_NAME OnionHEN)
add_executable(${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}.elf")

# Finalize main target sources
target_compile_options(${PROJECT_NAME} PUBLIC
	$<$<COMPILE_LANGUAGE:C>:${C_DEFS} ${C_FLAGS}>
	$<$<COMPILE_LANGUAGE:CXX>:${CXX_DEFS} ${CXX_FLAGS}>
	$<$<COMPILE_LANGUAGE:ASM>:${ASM_FLAGS}>
)

set(LZMA_ROOT "${ONIONHEN_THIRD_PARTY_DIR}/7zip_sdk/C")
set(LZMA_LIB ${LZMA_ROOT}/LzmaLib.c ${LZMA_ROOT}/Alloc.c
				${LZMA_ROOT}/LzmaEnc.c
				${LZMA_ROOT}/LzFind.c
				${LZMA_ROOT}/LzFindMt.c
				${LZMA_ROOT}/CpuArch.c
				${LZMA_ROOT}/LzmaDec.c
				${LZMA_ROOT}/Threads.c
				${LZMA_ROOT}/LzFindOpt.c)

# main.c only — initial bootstrap is sent to external elfldr 9021.
set(SrcFiles ${D_SRC}/main.c ${LZMA_LIB})

set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS} ")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-keyword-macro -fPIC -fPIE -march=znver2 -Wall -Werror -fstack-protector-all")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}")

target_sources(${PROJECT_NAME} PRIVATE ${SrcFiles})
set_source_files_properties(
	${D_SRC}/main.c
	PROPERTIES OBJECT_DEPENDS
		"${ONIONHEN_OUT_BIN}/bootstrapper.elf.lzma;${ONIONHEN_OUT_BIN}/bootstrapper.elf.lzma.size"
)
onion_target_debug_define(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE
	"${D_CWD}/include"
	"${LZMA_ROOT}"
	"${PROJECT_ROOT}/libonion_platform/include"
)
target_link_libraries(${PROJECT_NAME} PRIVATE onion_platform onion_conflict)
add_dependencies(${PROJECT_NAME} onion_platform onion_conflict)
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
target_compile_definitions(${PROJECT_NAME} PRIVATE
	ONIONHEN_BOOTSTRAPPER_LZMA="${ONIONHEN_OUT_BIN}/bootstrapper.elf.lzma"
	ONIONHEN_BOOTSTRAPPER_SIZE="${ONIONHEN_OUT_BIN}/bootstrapper.elf.lzma.size"
)
add_dependencies(${PROJECT_NAME} bootstrapper_packed)

# Add post-build command
onion_strip_debug(${PROJECT_NAME})
