if(NOT DEFINED REPO_ROOT OR NOT DEFINED OUTPUT_FILE)
  message(FATAL_ERROR "REPO_ROOT and OUTPUT_FILE are required")
endif()

find_program(GIT_EXECUTABLE git)

set(_git_clean FALSE)
set(_git_tag "")
set(_git_sha "unknown")
set(_tag_result 1)

if(GIT_EXECUTABLE)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${REPO_ROOT}" status --porcelain
            --untracked-files=normal
    RESULT_VARIABLE _status_result
    OUTPUT_VARIABLE _status_output
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(_status_result EQUAL 0 AND _status_output STREQUAL "")
    set(_git_clean TRUE)
  endif()

  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${REPO_ROOT}" describe --tags
            --exact-match HEAD
    RESULT_VARIABLE _tag_result
    OUTPUT_VARIABLE _git_tag
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${REPO_ROOT}" rev-parse --short=4 HEAD
    RESULT_VARIABLE _sha_result
    OUTPUT_VARIABLE _git_sha
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(NOT _sha_result EQUAL 0 OR _git_sha STREQUAL "")
    set(_git_sha "unknown")
  endif()
endif()

if(_git_clean AND _tag_result EQUAL 0 AND NOT _git_tag STREQUAL "")
  if(_git_tag MATCHES "^[vV]")
    set(_version "${_git_tag}")
  else()
    set(_version "v${_git_tag}")
  endif()
else()
  set(_version "dirty-${_git_sha}")
endif()

# Keep the generated value safe inside both a C string and notification JSON.
string(REGEX REPLACE "[^A-Za-z0-9._+-]" "-" _version "${_version}")

string(CONCAT _contents
    "#pragma once\n\n"
    "/* Generated at build time. Do not edit. */\n"
    "#define ONIONHEN_VERSION \"${_version}\"\n"
    "#define ONIONHEN_AUTHOR \"麒麟/0xp0co & kvnhrt\"\n")

get_filename_component(_output_dir "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_dir}")

set(_write_header TRUE)
if(EXISTS "${OUTPUT_FILE}")
  file(READ "${OUTPUT_FILE}" _existing_contents)
  if(_existing_contents STREQUAL _contents)
    set(_write_header FALSE)
  endif()
endif()

if(_write_header)
  file(WRITE "${OUTPUT_FILE}" "${_contents}")
endif()

message(STATUS "OnionHEN version: ${_version}")
