# cmake/ANTLR4Tool.cmake
#
# Locates the ANTLR4 code-generation tool.
#
# Strategy:
#   1. Check for `antlr4` in PATH (installed via antlr4-tools or manually),
#      and use it if it reports ANTLR4_TOOL_VERSION (generated code must
#      match the C++ runtime CMakeLists.txt fetches).
#   2. Otherwise, require Java >= 11 (ANTLR 4.13) and download the official
#      complete JAR.
#
# After inclusion, the following are set:
#   ANTLR4_TOOL_COMMAND  — CMake list for use in add_custom_command(COMMAND ...)
#   ANTLR4_TOOL_VERSION  — version string (used to match the C++ runtime)

set(ANTLR4_TOOL_VERSION "4.13.2")

find_program(ANTLR4_EXECUTABLE antlr4)

set(_ANTLR4_USE_EXECUTABLE FALSE)
if(ANTLR4_EXECUTABLE)
    # Run without arguments it prints "ANTLR Parser Generator  Version X".
    # (The antlr4-tools wrapper may download its JAR on the first run.)
    execute_process(
        COMMAND "${ANTLR4_EXECUTABLE}"
        OUTPUT_VARIABLE _antlr4_banner
        ERROR_VARIABLE  _antlr4_banner
        TIMEOUT 300
    )
    string(REGEX MATCH "Version ([0-9.]+)" _antlr4_match "${_antlr4_banner}")
    if(CMAKE_MATCH_1 STREQUAL ANTLR4_TOOL_VERSION)
        set(_ANTLR4_USE_EXECUTABLE TRUE)
    else()
        message(WARNING
            "${ANTLR4_EXECUTABLE} is ANTLR '${CMAKE_MATCH_1}', not "
            "${ANTLR4_TOOL_VERSION} (the C++ runtime's version) -- using the "
            "${ANTLR4_TOOL_VERSION} JAR instead. With antlr4-tools, set "
            "ANTLR4_TOOLS_ANTLR_VERSION=${ANTLR4_TOOL_VERSION} to use it.")
    endif()
endif()

if(_ANTLR4_USE_EXECUTABLE)
    message(STATUS "Found antlr4 tool: ${ANTLR4_EXECUTABLE} (${ANTLR4_TOOL_VERSION})")
    set(ANTLR4_TOOL_COMMAND "${ANTLR4_EXECUTABLE}")
else()
    if(NOT ANTLR4_EXECUTABLE)
        message(STATUS "antlr4 not found in PATH — will use JAR fallback")
    endif()

    # Java runtime is required to execute the JAR; ANTLR 4.13 needs 11+.
    find_package(Java 11 REQUIRED COMPONENTS Runtime)
    message(STATUS "Found Java: ${Java_JAVA_EXECUTABLE} (${Java_VERSION_STRING})")

    set(_ANTLR4_JAR_URL
        "https://www.antlr.org/download/antlr-${ANTLR4_TOOL_VERSION}-complete.jar")
    set(_ANTLR4_JAR
        "${CMAKE_BINARY_DIR}/antlr-${ANTLR4_TOOL_VERSION}-complete.jar")

    if(NOT EXISTS "${_ANTLR4_JAR}")
        message(STATUS "Downloading ANTLR4 ${ANTLR4_TOOL_VERSION} JAR...")
        file(DOWNLOAD
            "${_ANTLR4_JAR_URL}"
            "${_ANTLR4_JAR}"
            SHOW_PROGRESS
            STATUS _dl_status
        )
        list(GET _dl_status 0 _dl_code)
        list(GET _dl_status 1 _dl_msg)
        if(NOT _dl_code EQUAL 0)
            message(FATAL_ERROR
                "Failed to download ANTLR4 JAR from ${_ANTLR4_JAR_URL}: ${_dl_msg}")
        endif()
        message(STATUS "Downloaded ANTLR4 JAR to ${_ANTLR4_JAR}")
    else()
        message(STATUS "Using cached ANTLR4 JAR: ${_ANTLR4_JAR}")
    endif()

    set(ANTLR4_TOOL_COMMAND "${Java_JAVA_EXECUTABLE}" -jar "${_ANTLR4_JAR}")
endif()
