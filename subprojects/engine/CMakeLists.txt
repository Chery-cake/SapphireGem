# subprojects/engine/CMakeLists.txt

project(engine VERSION 1.0.0)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c*")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h*")

add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    #core
    #device
    #window
    #render
    #runtime
    ${EXTERNAL_LIBS}
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    BUILD_RPATH "${OUTPUT_DIR}/lib;${wasmtime_SOURCE_DIR}/lib"
    INSTALL_RPATH "$ORIGIN/lib"
)

target_compile_definitions(${PROJECT_NAME} PRIVATE ENGINE_EXPORTS)

# Copy assets to output directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets
    ${OUTPUT_DIR}/assets
    COMMENT "Copying assets to output directory"
)

# Copy wasmtime library to output lib directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${wasmtime_SOURCE_DIR}/lib/libwasmtime.so
    ${OUTPUT_DIR}/lib/
    COMMENT "Copying wasmtime library to output directory"
)

# Copy slang library to output lib directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${slang_SOURCE_DIR}/lib/libslang.so
    ${OUTPUT_DIR}/lib/
    COMMENT "Copying slang library to output directory"
)
