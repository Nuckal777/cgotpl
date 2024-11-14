add_executable(cli main.c)
set_property(TARGET cli PROPERTY OUTPUT_NAME cgotpl)
target_link_libraries(cli PRIVATE cgotpl)
