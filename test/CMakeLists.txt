include(CTest)

add_executable(test_map map.c)
target_link_libraries(test_map PRIVATE cgotpl)

add_executable(test_stream stream.c)
target_link_libraries(test_stream PRIVATE cgotpl)

add_executable(test_json json.c)
target_link_libraries(test_json PRIVATE cgotpl)

add_executable(test_template template.c)
target_link_libraries(test_template PRIVATE cgotpl)

add_test(NAME TestMap COMMAND test_map)
add_test(NAME TestStream COMMAND test_stream)
add_test(NAME TestJson COMMAND test_json)
add_test(NAME TestTemplate COMMAND test_template)
add_custom_target(
    test_all COMMAND ${CMAKE_CTEST_COMMAND}
    DEPENDS ${TESTS}
    COMMENT "Run all tests"
)
