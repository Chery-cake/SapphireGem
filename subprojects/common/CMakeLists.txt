project(common)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c*")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h*")

add_library(${PROJECT_NAME} SHARED
    ${SOURCES}
    ${HEADERS}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    btp
    vulkan
    vma
    glfw
)

# For Windows builds with MinGW, we need to link against additional standard library components
# to support std::print functionality (requires __open_terminal and __write_to_terminal symbols)
# Note: This requires GCC 13+ with the experimental C++ library
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Try to link the experimental C++ features library for std::print support
    # If this fails, users may need to update their MinGW installation or avoid using std::print
    target_link_libraries(${PROJECT_NAME} PRIVATE
        -lstdc++exp
    )
endif()

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
