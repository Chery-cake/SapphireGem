# ==============================================================================
# Main Linkage Test
# ==============================================================================

# Test that the core modules can be linked together properly
add_executable(test_main_linkage test_main_linkage.cpp)
target_link_libraries(test_main_linkage PRIVATE core device window)
target_compile_definitions(test_main_linkage PRIVATE
    $<$<CONFIG:Debug>:ENGINE_DEBUG>
)
add_test(NAME main_linkage COMMAND test_main_linkage)

message(STATUS "Main linkage test configured")
