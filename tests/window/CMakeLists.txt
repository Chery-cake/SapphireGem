# ==============================================================================
# Window Module Tests
# ==============================================================================

# --- Window Structures Tests ---
add_executable(test_window_structures test_window_structures.cpp)
target_link_libraries(test_window_structures PRIVATE window device core)
target_compile_definitions(test_window_structures PRIVATE
    $<$<CONFIG:Debug>:ENGINE_DEBUG>
)
add_test(NAME window_structures COMMAND test_window_structures)

# --- WindowManager Tests ---
# These tests check WindowManager initialization.
# Some tests may be skipped at runtime if no display is available.
add_executable(test_window_manager test_window_manager.cpp)
target_link_libraries(test_window_manager PRIVATE window device core)
target_compile_definitions(test_window_manager PRIVATE
    $<$<CONFIG:Debug>:ENGINE_DEBUG>
)
add_test(NAME window_manager COMMAND test_window_manager)

# --- Face Material Tests ---
add_executable(test_face_material test_face_material.cpp)
target_link_libraries(test_face_material PRIVATE window device core)
target_compile_definitions(test_face_material PRIVATE
    $<$<CONFIG:Debug>:ENGINE_DEBUG>
)
add_test(NAME window_face_material COMMAND test_face_material)

message(STATUS "Window tests configured")
