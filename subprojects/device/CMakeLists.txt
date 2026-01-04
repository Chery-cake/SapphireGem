# subprojects/device/CMakeLists.txt

project(device VERSION 1.0.0)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c*")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h*")

add_library(${PROJECT_NAME} SHARED
    ${SOURCES}
    ${HEADERS}
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Use fetched Vulkan headers (must come first to take precedence over system headers)
target_include_directories(${PROJECT_NAME} PUBLIC ${VULKAN_HEADERS_INCLUDE_DIR})

# VMA include directory
target_include_directories(${PROJECT_NAME} PUBLIC ${VMA_INCLUDE_DIR})

target_link_libraries(${PROJECT_NAME} PUBLIC
    core
    vma_impl
    vulkan_hpp_dispatch
    ${EXTERNAL_LIBS}
)

# Link against dl for dlopen/dlsym on Linux (required for dynamic loading)
if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS})
endif()

# Export all symbols since we're the source of vulkan wrapper functions
set_target_properties(${PROJECT_NAME} PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET default
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_compile_definitions(${PROJECT_NAME} PRIVATE DEVICE_EXPORTS)
