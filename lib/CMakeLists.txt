add_library(
    cgotpl
    "${CMAKE_CURRENT_SOURCE_DIR}/encode.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/map.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/stream.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/json.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/template.c"
)
target_include_directories(cgotpl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cgotpl PUBLIC m)
