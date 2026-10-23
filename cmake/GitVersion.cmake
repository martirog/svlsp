# Writes OUTPUT (a header defining SVLSP_GIT_VERSION) from `git describe`
# in SOURCE_DIR. Run with `cmake -P` on every build (target
# svlsp_git_version); the file is only rewritten when the version changed,
# so an unchanged checkout recompiles nothing.
set(_version "unknown")
if(GIT_EXECUTABLE AND EXISTS "${SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --always --dirty --abbrev=7
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE _describe
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _result
    )
    if(_result EQUAL 0)
        set(_version "${_describe}")
    endif()
endif()

set(_content "#pragma once\n#define SVLSP_GIT_VERSION \"${_version}\"\n")
set(_old "")
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" _old)
endif()
if(NOT _old STREQUAL _content)
    file(WRITE "${OUTPUT}" "${_content}")
endif()
