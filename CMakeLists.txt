project(cgotpl LANGUAGES C)

include(CTest)

set(C_STANDARD 99)

add_library(lib lib/encode.c lib/map.c lib/stream.c lib/json.c lib/template.c)

add_executable(test_map test/map.c)
target_link_libraries(test_map PUBLIC lib)
target_include_directories(test_map PUBLIC "lib")

add_executable(test_stream test/stream.c)
target_link_libraries(test_stream PUBLIC lib)
target_include_directories(test_stream PUBLIC "lib")

add_executable(test_json test/json.c)
target_link_libraries(test_json PUBLIC lib)
target_include_directories(test_json PUBLIC "lib")

add_executable(test_template test/template.c)
target_link_libraries(test_template PUBLIC lib)
target_include_directories(test_template PUBLIC "lib")

add_test(NAME TestMap COMMAND test_map)
add_test(NAME TestStream COMMAND test_stream)
add_test(NAME TestJson COMMAND test_json)
add_test(NAME TestTemplate COMMAND test_template)
add_custom_target(
    check COMMAND ${CMAKE_CTEST_COMMAND}
    DEPENDS test_map test_stream test_json test_template
)
