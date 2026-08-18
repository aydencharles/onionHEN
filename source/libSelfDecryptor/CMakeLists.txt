###################################################################################################
# libSelfDecryptor (ported from etaHEN) — SELF decryption via kernel pager hijack.
# Sources: selfpager.c (pager-swap + mmap_self), sbl.c/authmgr.c (SBL mailbox path),
# main.c (file/dir decryption helpers).
# @authors ChendoChap, Specter, Znullptr, LightningMods
###################################################################################################

cmake_minimum_required (VERSION 3.20)

set(basename "SelfDecryptor")
project(${basename} C)

set(D_CWD "${PROJECT_SOURCE_DIR}")

onion_module_prologue(C_STANDARD 17)

set(D_SRC ${D_CWD}/source)

set(SrcFiles
  ${D_SRC}/main.c
  ${D_SRC}/selfpager.c
  ${D_SRC}/sbl.c
  ${D_SRC}/authmgr.c
)

add_library(${PROJECT_NAME} STATIC ${SrcFiles})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")

target_compile_options(${PROJECT_NAME} PUBLIC
	$<$<COMPILE_LANGUAGE:C>:${C_DEFS} ${C_FLAGS}>
)

set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS_MARCH} ")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_POSIX_SOURCE -D_POSIX_C_SOURCE=200112 -D__BSD_VISIBLE=1 -D__XSI_VISIBLE=500")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-builtin -nostdlib -Wall")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -gfull -O3 -march=znver2 -Wall -Werror")

target_include_directories(${PROJECT_NAME} PRIVATE
  "${D_CWD}/include"
  "${PROJECT_ROOT}/include"
)
