# ==============================================================================
# Core Module Tests
# ==============================================================================

# --- BumpAllocator Tests ---
add_executable(test_bump_allocator test_bump_allocator.cpp)
target_link_libraries(test_bump_allocator PRIVATE core)
target_compile_definitions(test_bump_allocator PRIVATE
    $<$<CONFIG:Debug>:ENGINE_DEBUG>
)
add_test(NAME core_bump_allocator COMMAND test_bump_allocator)

# --- StockAllocator Tests ---
add_executable(test_stack_allocator test_stack_allocator.cpp)
target_link_libraries(test_stack_allocator PRIVATE core)
target_compile_definitions(test_stack_allocator PRIVATE
    $<$<CONFIG:Debug>:ENGINE_DEBUG>
)
add_test(NAME core_stack_allocator COMMAND test_stack_allocator)

# --- PoolAllocator Tests ---
add_executable(test_pool_allocator test_pool_allocator.cpp)
target_link_libraries(test_pool_allocator PRIVATE core)
target_compile_definitions(test_pool_allocator PRIVATE
    $<$<CONFIG:Debug>:ENGINE_DEBUG>
)
add_test(NAME core_pool_allocator COMMAND test_pool_allocator)

# --- Config Tests ---
add_executable(test_config test_config.cpp)
target_link_libraries(test_config PRIVATE core)
target_compile_definitions(test_config PRIVATE
    $<$<CONFIG:Debug>:ENGINE_DEBUG>
)
add_test(NAME core_config COMMAND test_config)

# --- ThreadManager Tests ---
add_executable(test_thread_manager test_thread_manager.cpp)
target_link_libraries(test_thread_manager PRIVATE core)
target_compile_definitions(test_thread_manager PRIVATE
    $<$<CONFIG:Debug>:ENGINE_DEBUG>
)
add_test(NAME core_thread_manager COMMAND test_thread_manager)

# --- ResourceRegistry Tests ---
add_executable(test_resource_registry test_resource_registry.cpp)
target_link_libraries(test_resource_registry PRIVATE core)
target_compile_definitions(test_resource_registry PRIVATE
    $<$<CONFIG:Debug>:ENGINE_DEBUG>
)
add_test(NAME core_resource_registry COMMAND test_resource_registry)

# --- Signal Tests ---
add_executable(test_signal test_signal.cpp)
target_link_libraries(test_signal PRIVATE core)
target_compile_definitions(test_signal PRIVATE
    $<$<CONFIG:Debug>:ENGINE_DEBUG>
)
add_test(NAME core_signal COMMAND test_signal)

message(STATUS "Core tests configured")
