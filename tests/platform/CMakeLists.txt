# ==============================================================================
# Platform-Specific Tests
# ==============================================================================

# --- Linux Platform Tests ---
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(test_platform_linux test_platform_linux.cpp)
    target_link_libraries(test_platform_linux PRIVATE core device)
    target_compile_definitions(test_platform_linux PRIVATE
        $<$<CONFIG:Debug>:ENGINE_DEBUG>
    )
    target_configure_platform(test_platform_linux)
    add_test(NAME platform_linux COMMAND test_platform_linux)
    message(STATUS "Linux platform tests configured")
endif()

# --- Windows Platform Tests ---
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_executable(test_platform_windows test_platform_windows.cpp)
    target_link_libraries(test_platform_windows PRIVATE core device)
    target_compile_definitions(test_platform_windows PRIVATE
        $<$<CONFIG:Debug>:ENGINE_DEBUG>
    )
    target_configure_platform(test_platform_windows)
    add_test(NAME platform_windows COMMAND test_platform_windows)
    message(STATUS "Windows platform tests configured")
endif()

# --- macOS Platform Tests ---
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR APPLE)
    add_executable(test_platform_macos test_platform_macos.cpp)
    target_link_libraries(test_platform_macos PRIVATE core device)
    target_compile_definitions(test_platform_macos PRIVATE
        $<$<CONFIG:Debug>:ENGINE_DEBUG>
    )
    target_configure_platform(test_platform_macos)
    add_test(NAME platform_macos COMMAND test_platform_macos)
    message(STATUS "macOS platform tests configured")
endif()
