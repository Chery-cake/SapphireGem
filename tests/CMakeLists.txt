# ==============================================================================
# Tests Configuration
# ==============================================================================

cmake_minimum_required(VERSION 3.25)

# ==============================================================================
# Core Module Tests
# ==============================================================================

# Config test
add_executable(test_config
    ${CMAKE_CURRENT_SOURCE_DIR}/test_config.cpp
)
target_link_libraries(test_config PRIVATE core)
target_compile_definitions(test_config PRIVATE
    $<$<CONFIG:Debug>:ENGINE_DEBUG>
)
target_compile_options(test_config PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
add_test(NAME ConfigTest COMMAND test_config)

# ThreadManager test
add_executable(test_thread_manager
    ${CMAKE_CURRENT_SOURCE_DIR}/test_thread_manager.cpp
)
target_link_libraries(test_thread_manager PRIVATE core)
target_compile_definitions(test_thread_manager PRIVATE
    $<$<CONFIG:Debug>:ENGINE_DEBUG>
)
target_compile_options(test_thread_manager PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
add_test(NAME ThreadManagerTest COMMAND test_thread_manager)

# MemoryManager test
add_executable(test_memory_manager
    ${CMAKE_CURRENT_SOURCE_DIR}/test_memory_manager.cpp
)
target_link_libraries(test_memory_manager PRIVATE core)
target_compile_definitions(test_memory_manager PRIVATE
    $<$<CONFIG:Debug>:ENGINE_DEBUG>
)
target_compile_options(test_memory_manager PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
add_test(NAME MemoryManagerTest COMMAND test_memory_manager)

message(STATUS "Configured: Tests")
