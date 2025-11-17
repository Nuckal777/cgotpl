cmake_minimum_required(VERSION 3.16)
project(cgotpl LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(C_STANDARD 99)

add_subdirectory(lib)
add_subdirectory(cli)
add_subdirectory(test)
add_subdirectory(jsontest)

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
