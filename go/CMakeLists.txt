add_custom_target(
    gotemplate COMMAND ${GO} build -o "${CMAKE_CURRENT_BINARY_DIR}/gotemplate" main.go
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    SOURCES main.go go.mod
    COMMENT "Building Go executable gotemplate"
)
