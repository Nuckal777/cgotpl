add_library(fuzz_cgotpl $<TARGET_PROPERTY:cgotpl,SOURCES>)
target_compile_options(fuzz_cgotpl PUBLIC -fsanitize=fuzzer,address,undefined)
target_link_libraries(fuzz_cgotpl PUBLIC -fsanitize=fuzzer,address,undefined)
target_include_directories(fuzz_cgotpl PUBLIC $<TARGET_PROPERTY:cgotpl,INCLUDE_DIRECTORIES>)

add_executable(fuzz_json json.c)
target_link_libraries(fuzz_json PRIVATE fuzz_cgotpl)

add_executable(fuzz_template template.c)
target_link_libraries(fuzz_template PRIVATE fuzz_cgotpl)

add_custom_target(
    fuzz
    COMMAND "${CMAKE_CURRENT_BINARY_DIR}/fuzz_json" -max_total_time=20 "-dict=${CMAKE_CURRENT_SOURCE_DIR}/json.dict" "${CMAKE_CURRENT_SOURCE_DIR}/json.corpus"
    COMMAND "${CMAKE_CURRENT_BINARY_DIR}/fuzz_template" -max_total_time=20 "-dict=${CMAKE_CURRENT_SOURCE_DIR}/template.dict" "${CMAKE_CURRENT_SOURCE_DIR}/template.corpus"
    DEPENDS fuzz_json fuzz_template
)
