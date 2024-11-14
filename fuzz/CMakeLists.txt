add_executable(fuzz_json json.c)
target_link_libraries(fuzz_json PUBLIC fuzz_cgotpl)
target_include_directories(fuzz_json PUBLIC "${PROJECT_SOURCE_DIR}/lib")
target_compile_options(fuzz_json PRIVATE -fsanitize=fuzzer,address,undefined)
target_link_libraries(fuzz_json PRIVATE -fsanitize=fuzzer,address,undefined)

add_executable(fuzz_template template.c)
target_link_libraries(fuzz_template PUBLIC fuzz_cgotpl)
target_include_directories(fuzz_template PUBLIC "${PROJECT_SOURCE_DIR}/lib")
target_compile_options(fuzz_template PRIVATE -fsanitize=fuzzer,address,undefined)
target_link_libraries(fuzz_template PRIVATE -fsanitize=fuzzer,address,undefined)

add_custom_target(
    fuzz
    COMMAND "${CMAKE_CURRENT_BINARY_DIR}/fuzz_json" -max_total_time=20 "-dict=${CMAKE_CURRENT_SOURCE_DIR}/json.dict" "${CMAKE_CURRENT_SOURCE_DIR}/json.corpus"
    COMMAND "${CMAKE_CURRENT_BINARY_DIR}/fuzz_template" -max_total_time=20 "-dict=${CMAKE_CURRENT_SOURCE_DIR}/template.dict" "${CMAKE_CURRENT_SOURCE_DIR}/template.corpus"
    DEPENDS fuzz_json fuzz_template
)
