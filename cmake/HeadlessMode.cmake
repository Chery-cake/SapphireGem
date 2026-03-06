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
#
# Dependencies are configured as follows:
# - SDL3: Built without X11/Wayland (SDL_UNIX_CONSOLE_BUILD=ON)
# - SDL_image: Still available for image loading (CPU-side)
# - Vulkan: Still available for compute and headless rendering
# ==============================================================================

option(ENGINE_HEADLESS "Build without window/display output (headless mode)" OFF)

if(ENGINE_HEADLESS)
    message(STATUS "")
    message(STATUS "========================================")
    message(STATUS " Headless Mode: ENABLED")
    message(STATUS "========================================")
    message(STATUS " - Window creation disabled")
    message(STATUS " - Vulkan surface/swapchain disabled")
    message(STATUS " - SDL video subsystem (X11/Wayland) disabled")
    message(STATUS " - GPU compute and image loading remain available")
    message(STATUS "========================================")
    message(STATUS "")

    # Set a global compile definition so all targets see ENGINE_HEADLESS
    add_compile_definitions(ENGINE_HEADLESS)

    # Configure SDL3 for headless/console build (no X11/Wayland required)
    set(SDL_X11 OFF CACHE BOOL "Disable X11 for headless build" FORCE)
    set(SDL_WAYLAND OFF CACHE BOOL "Disable Wayland for headless build" FORCE)
    set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "Allow SDL build without display server" FORCE)

    # Use vendored freetype/harfbuzz for SDL_ttf so system libraries are not required
    set(SDLTTF_VENDORED ON CACHE BOOL "Use vendored freetype/harfbuzz for headless build" FORCE)
endif()

# ==============================================================================
# Print headless mode status
# ==============================================================================
function(print_headless_mode_info)
    message(STATUS " Headless Mode: ${ENGINE_HEADLESS}")
endfunction()
