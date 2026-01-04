# subprojects/render/CMakeLists.txt

project(render VERSION 1.0.0)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c*")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h*")

add_library(${PROJECT_NAME} SHARED
    ${SOURCES}
    ${HEADERS}
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SLANG_ROOT}/include
)

# Add Vulkan headers and vulkan_hpp for direct Vulkan calls
target_include_directories(${PROJECT_NAME} PUBLIC ${VULKAN_HEADERS_INCLUDE_DIR})
target_include_directories(${PROJECT_NAME} PUBLIC ${VULKAN_HPP_INCLUDE_DIR})
target_include_directories(${PROJECT_NAME} PUBLIC ${VMA_INCLUDE_DIR})

# Note: render links against device which has all Vulkan/VMA implementation
target_link_libraries(${PROJECT_NAME} PUBLIC
    stb_impl
    core
    device
    slang_lib
    vulkan_hpp_dispatch
    vma_impl
    ${EXTERNAL_LIBS}
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

target_compile_definitions(${PROJECT_NAME} PRIVATE RENDER_EXPORTS)
