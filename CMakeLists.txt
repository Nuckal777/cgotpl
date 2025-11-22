cmake_minimum_required(VERSION 3.16)
project(cgotpl LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(C_STANDARD 99)

find_package(Git)

if(GIT_EXECUTABLE)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --abbrev=7 --dirty
        OUTPUT_VARIABLE CGOTPL_VERSION
        RESULT_VARIABLE ERROR_CODE
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

if(CGOTPL_VERSION STREQUAL "")
    set(CGOTPL_VERSION unknown)
    message(WARNING "Failed to determine version from git. Using \"${CGOTPL_VERSION}\".")
endif()
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/include/version.h.in" "${CMAKE_CURRENT_SOURCE_DIR}/include/version.h" @ONLY)

add_subdirectory(lib)
add_subdirectory(cli)
add_subdirectory(jsontest)
add_subdirectory(test)

if ("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
    add_subdirectory(fuzz)
endif()

find_program(GO go)
if(NOT "${GO}" MATCHES "GO-NOTFOUND")
    add_subdirectory(go)
    add_custom_target(
        check COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/cmp.sh" "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "Comparing cgotpl with golangs text/template"
        DEPENDS test_all cli gotemplate
        SOURCES cmp.sh
    )
endif()
