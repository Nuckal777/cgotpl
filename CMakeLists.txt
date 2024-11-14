cmake_minimum_required(VERSION 3.16)
project(cgotpl LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

set(C_STANDARD 99)

add_library(cgotpl lib/encode.c lib/map.c lib/stream.c lib/json.c lib/template.c)

add_library(fuzz_cgotpl $<TARGET_PROPERTY:cgotpl,SOURCES>)
target_compile_options(fuzz_cgotpl PRIVATE -fsanitize=fuzzer,address,undefined)
target_link_libraries(fuzz_cgotpl PRIVATE -fsanitize=fuzzer,address,undefined)

add_executable(cli cli/main.c)
set_property(TARGET cli PROPERTY OUTPUT_NAME cgotpl)
target_link_libraries(cli PUBLIC cgotpl)
target_include_directories(cli PUBLIC "lib")

add_subdirectory(fuzz)

add_executable(test_map test/map.c)
target_link_libraries(test_map PUBLIC cgotpl)
target_include_directories(test_map PUBLIC "lib")

add_executable(test_stream test/stream.c)
target_link_libraries(test_stream PUBLIC cgotpl)
target_include_directories(test_stream PUBLIC "lib")

add_executable(test_json test/json.c)
target_link_libraries(test_json PUBLIC cgotpl)
target_include_directories(test_json PUBLIC "lib")

add_executable(test_template test/template.c)
target_link_libraries(test_template PUBLIC cgotpl)
target_include_directories(test_template PUBLIC "lib")

add_test(NAME TestMap COMMAND test_map)
add_test(NAME TestStream COMMAND test_stream)
add_test(NAME TestJson COMMAND test_json)
add_test(NAME TestTemplate COMMAND test_template)
add_custom_target(
    test_all COMMAND ${CMAKE_CTEST_COMMAND}
    DEPENDS test_map test_stream test_json test_template
)

add_custom_target(
    gotemplate COMMAND go build -o "${CMAKE_CURRENT_BINARY_DIR}/gotemplate" main.go
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/go"
    SOURCES go/main.go go/go.mod
    COMMENT "Building Go executable gotemplate"
)

add_custom_target(
    check COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/cmp.sh" "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Comparing cgotpl with golangs text/template"
    DEPENDS test_all cli gotemplate
    SOURCES cmp.sh
)
