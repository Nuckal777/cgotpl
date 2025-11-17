add_executable(jsontest main.c)
target_link_libraries(jsontest PRIVATE cgotpl)
file(GLOB TESTFILES "JSONTestSuite/test_parsing/*.json")
add_custom_target(
    jsontest_all COMMAND $<TARGET_FILE:jsontest> ${TESTFILES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    SOURCES ${TESTFILES}
    COMMENT "Running jsontest against JSONTestSuite"
    DEPENDS jsontest
)
