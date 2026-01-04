# DefinitionsForVersion.cmake
# Compile definitions and options for different build types

# ============================================================================
# Vulkan definitions (using vulkan.hpp dynamic dispatch loader)
# ============================================================================
add_compile_definitions(
    # Disable Vulkan prototypes - we use dynamic loading via vulkan.hpp
    VK_NO_PROTOTYPES
    # VMA configuration for dynamic Vulkan functions
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1
    # Do NOT use GLFW_INCLUDE_VULKAN - we include vulkan.hpp explicitly
)

# ============================================================================
# Platform-specific Vulkan surface definitions
# ============================================================================
if(WIN32)
    add_compile_definitions(VK_USE_PLATFORM_WIN32_KHR)
elseif(APPLE)
    add_compile_definitions(VK_USE_PLATFORM_MACOS_MVK)
elseif(UNIX)
    add_compile_definitions(VK_USE_PLATFORM_XLIB_KHR)
endif()

# ============================================================================
# Build type specific settings
# ============================================================================
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    # Optimizations
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)  # LTO

    # Strip symbols where applicable
    if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT WIN32)
        add_link_options(-s)  # GCC/Clang: strip symbols
    endif()

    add_compile_definitions(
        NDEBUG  # Disable assertions
    )
else()
    # Compiler flags
    add_compile_definitions(DEBUG_ENABLED)  # Custom define for debug code
    add_compile_options(
        "$<$<C_COMPILER_ID:MSVC>:/Zi>"     # MSVC debug symbols
        "$<$<CXX_COMPILER_ID:MSVC>:/Zi>"
        "$<$<NOT:$<C_COMPILER_ID:MSVC>>:-g3>"  # GCC/Clang debug symbols
        "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-g3>"
    )

    # Linker flags
    add_link_options(
        "$<$<C_COMPILER_ID:MSVC>:/DEBUG>"  # MSVC PDB generation
    )

    # Disable optimizations
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0")

    # Enable assertions (remove in production)
    add_compile_definitions(ENABLE_ASSERTIONS)

    # Sanitizers are not available for MinGW cross-compilation
    if(NOT (CMAKE_CROSSCOMPILING AND WIN32))
        add_compile_options(
            -fsanitize=address
            -fsanitize=undefined
            -fno-sanitize-recover=all
            -fno-omit-frame-pointer
        )
        add_link_options(
            -fsanitize=address
            -fsanitize=undefined
        )

        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            add_compile_options(-fno-var-tracking)
        endif()

        set(SUPPRESSIONS_CONFIG_FILE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/suppressions.txt")
        message("Memory leak testing (sanitizers) enabled")
    else()
        message(WARNING "Memory leak testing (sanitizers) not available for MinGW cross-compilation")
    endif()

endif()

# ============================================================================
# Sanitizer suppressions file
# ============================================================================
if(NOT "${SUPPRESSIONS_CONFIG_FILE}" STREQUAL "")

file(WRITE ${SUPPRESSIONS_CONFIG_FILE} "# Auto-generated suppressions configuration
# Fontconfig suppressions
leak:libfontconfig.so
leak:FcPatternObjectInsertElt
leak:FcValueListCreate
leak:FcValueSave
leak:FcConfigValues
leak:FcLangSetCreate
leak:FcRangeCreateDouble
leak:FcValueListPrepend
leak:FcValueListDuplicate
leak:FcFontRenderPrepare
leak:FcPatternObjectAdd
leak:FcPatternObjectAddWithBinding
leak:FcPatternAddBool
leak:FcPatternAddInteger
leak:FcPatternAddString

# Wayland suppressions
leak:libwayland-client.so
leak:proxy_create
leak:create_outgoing_proxy
leak:wl_proxy_marshal_array_flags
leak:zalloc

# Pango suppressions
leak:libpango
leak:pango_cairo_fc_font_map
leak:pango_cairo_fc_font_map_fontset_key_substitute

# X11/GLib suppressions
leak:libX11
leak:libGL
leak:libgio
leak:libglib
")

endif()
