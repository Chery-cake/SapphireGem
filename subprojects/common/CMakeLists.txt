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
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(${PROJECT_NAME} PRIVATE
        -lstdc++exp  # Experimental C++ features library for std::print support
    )
endif()

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
