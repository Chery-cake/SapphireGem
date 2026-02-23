# ==============================================================================
# HeadlessMode.cmake - Compile Without Window/Display Output
# ==============================================================================
# Provides an option to build the engine without any window or display output.
# When enabled, SDL video subsystem, Vulkan surface/swapchain creation, and
# all window-dependent rendering code are disabled at compile time.
#
# Usage:
#   cmake -B build -DENGINE_HEADLESS=ON
#
# This sets the ENGINE_HEADLESS preprocessor definition globally so that
# all targets (engine libraries and the main application) can guard
# display-dependent code paths.
# ==============================================================================

option(ENGINE_HEADLESS "Build without window/display output (headless mode)" OFF)

if(ENGINE_HEADLESS)
    message(STATUS "")
    message(STATUS "========================================")
    message(STATUS " Headless Mode: ENABLED")
    message(STATUS "========================================")
    message(STATUS " - Window creation disabled")
    message(STATUS " - Vulkan surface/swapchain disabled")
    message(STATUS " - SDL video subsystem disabled")
    message(STATUS " - GPU compute and image loading remain available")
    message(STATUS "========================================")
    message(STATUS "")

    # Set a global compile definition so all targets see ENGINE_HEADLESS
    add_compile_definitions(ENGINE_HEADLESS)
endif()

# ==============================================================================
# Print headless mode status
# ==============================================================================
function(print_headless_mode_info)
    message(STATUS " Headless Mode: ${ENGINE_HEADLESS}")
endfunction()
