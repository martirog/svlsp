# cmake/ANTLR4Tool.cmake
#
# Locates the ANTLR4 code-generation tool.
#
# Strategy:
#   1. Check for `antlr4` in PATH (installed via antlr4-tools or manually).
#   2. If absent, require Java and download the official complete JAR.
#
# After inclusion, the following are set:
#   ANTLR4_TOOL_COMMAND  — CMake list for use in add_custom_command(COMMAND ...)
#   ANTLR4_TOOL_VERSION  — version string (used to match the C++ runtime)

set(ANTLR4_TOOL_VERSION "4.13.2")

find_program(ANTLR4_EXECUTABLE antlr4)

if(ANTLR4_EXECUTABLE)
    message(STATUS "Found antlr4 tool: ${ANTLR4_EXECUTABLE}")
    set(ANTLR4_TOOL_COMMAND "${ANTLR4_EXECUTABLE}")
else()
    message(STATUS "antlr4 not found in PATH — will use JAR fallback")

    # Java runtime is required to execute the JAR.
    find_package(Java REQUIRED COMPONENTS Runtime)
    message(STATUS "Found Java: ${Java_JAVA_EXECUTABLE}")

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
