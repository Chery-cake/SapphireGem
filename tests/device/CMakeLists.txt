# ==============================================================================
# Device Module Tests
# ==============================================================================

# --- Device Structures Tests ---
add_executable(test_device_structures test_device_structures.cpp)
target_link_libraries(test_device_structures PRIVATE device core)
target_compile_definitions(test_device_structures PRIVATE
    $<$<CONFIG:Debug>:ENGINE_DEBUG>
)
add_test(NAME device_structures COMMAND test_device_structures)

# --- VulkanInstance Tests ---
# These tests check VulkanInstance static methods and initialization.
# Some tests may be skipped at runtime if Vulkan is not available.
add_executable(test_vulkan_instance test_vulkan_instance.cpp)
target_link_libraries(test_vulkan_instance PRIVATE device core)
target_compile_definitions(test_vulkan_instance PRIVATE
    $<$<CONFIG:Debug>:ENGINE_DEBUG>
)
add_test(NAME device_vulkan_instance COMMAND test_vulkan_instance)

message(STATUS "Device tests configured")
