# ==============================================================================
# BuildConfiguration.cmake - Debug and Release Build Configuration
# ==============================================================================
# Provides comprehensive build settings for Debug and Release configurations.
# - Debug: Debug symbols, sanitizers, unobfuscated code
# - Release: Full optimizations, LTO, stripped symbols
# ==============================================================================

# ==============================================================================
# Sanitizer Suppression File Path
# ==============================================================================
set(SANITIZER_SUPPRESSION_DIR "${CMAKE_SOURCE_DIR}/cmake/sanitizers")
set(LSAN_SUPPRESSION_FILE "${SANITIZER_SUPPRESSION_DIR}/lsan.supp")
set(ASAN_SUPPRESSION_FILE "${SANITIZER_SUPPRESSION_DIR}/asan.supp")

# ==============================================================================
# Function to configure Debug build settings for a target
# ==============================================================================
function(target_configure_debug TARGET_NAME)
    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "target_configure_debug: Target '${TARGET_NAME}' does not exist")
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # Debug-specific compile options
        target_compile_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Debug>:
                -g3                     # Maximum debug information
                -O0                     # No optimization
                -fno-omit-frame-pointer # Keep frame pointers for better stack traces
                -fno-optimize-sibling-calls # Better stack traces
                -fstack-protector-strong # Stack overflow protection
            >
        )

        # Debug definitions
        target_compile_definitions(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Debug>:
                DEBUG
                _DEBUG
                ENGINE_DEBUG
            >
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Debug>:
                /Zi         # Debug information
                /Od         # No optimization
                /RTC1       # Runtime checks
                /GS         # Buffer security check
                /sdl        # Additional security checks
            >
        )

        target_compile_definitions(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Debug>:
                DEBUG
                _DEBUG
                ENGINE_DEBUG
            >
        )
    endif()
endfunction()

# ==============================================================================
# Function to configure Release build settings for a target
# ==============================================================================
function(target_configure_release TARGET_NAME)
    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "target_configure_release: Target '${TARGET_NAME}' does not exist")
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # Release-specific compile options - maximum optimization
        target_compile_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Release>:
                -O3                     # Maximum optimization
                -funroll-loops          # Unroll loops
                -ffunction-sections     # Place each function in its own section
                -fdata-sections         # Place each data item in its own section
                -fvisibility=hidden     # Hide symbols by default (obfuscation)
                -fvisibility-inlines-hidden # Hide inline function symbols
            >
        )

        # Optional: Optimize for current CPU (not suitable for distribution)
        if(ENGINE_OPTIMIZE_NATIVE)
            target_compile_options(${TARGET_NAME} PRIVATE
                $<$<CONFIG:Release>:
                    -march=native       # Optimize for current CPU
                    -mtune=native       # Tune for current CPU
                >
            )
        endif()

        # Optional: Enable fast-math (breaks IEEE 754 compliance)
        # WARNING: This may cause issues with physics simulations, financial
        # calculations, or any code requiring strict floating-point behavior.
        if(ENGINE_FAST_MATH)
            target_compile_options(${TARGET_NAME} PRIVATE
                $<$<CONFIG:Release>:-ffast-math>
            )
        endif()

        # Release definitions
        target_compile_definitions(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Release>:
                NDEBUG
                ENGINE_RELEASE
            >
        )

        # Linker options for Release
        target_link_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Release>:
                -Wl,--gc-sections       # Remove unused sections
                -Wl,--strip-all         # Strip all symbols
                -Wl,-s                  # Strip symbol table
            >
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Release>:
                /O2         # Maximum optimization
                /Ob2        # Inline expansion
                /Oi         # Intrinsic functions
                /Ot         # Favor fast code
                /GL         # Whole program optimization
                /Gy         # Function-level linking
            >
        )

        target_compile_definitions(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Release>:
                NDEBUG
                ENGINE_RELEASE
            >
        )

        target_link_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Release>:
                /LTCG       # Link-time code generation
                /OPT:REF    # Remove unreferenced code
                /OPT:ICF    # COMDAT folding
            >
        )
    endif()
endfunction()

# ==============================================================================
# Function to configure RelWithDebInfo build settings for a target
# ==============================================================================
function(target_configure_relwithdebinfo TARGET_NAME)
    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "target_configure_relwithdebinfo: Target '${TARGET_NAME}' does not exist")
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # RelWithDebInfo-specific compile options - optimization with debug info
        target_compile_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:RelWithDebInfo>:
                -O2                     # Good optimization
                -g                      # Debug information
                -ffunction-sections     # Place each function in its own section
                -fdata-sections         # Place each data item in its own section
            >
        )

        # RelWithDebInfo definitions
        target_compile_definitions(${TARGET_NAME} PRIVATE
            $<$<CONFIG:RelWithDebInfo>:
                NDEBUG
                ENGINE_RELEASE
            >
        )

        # Linker options for RelWithDebInfo
        target_link_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:RelWithDebInfo>:
                -Wl,--gc-sections       # Remove unused sections
            >
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:RelWithDebInfo>:
                /O2         # Good optimization
                /Zi         # Debug information
                /Ob1        # Inline expansion
                /Oi         # Intrinsic functions
            >
        )

        target_compile_definitions(${TARGET_NAME} PRIVATE
            $<$<CONFIG:RelWithDebInfo>:
                NDEBUG
                ENGINE_RELEASE
            >
        )
    endif()
endfunction()

# ==============================================================================
# Function to configure MinSizeRel build settings for a target
# ==============================================================================
function(target_configure_minsizerel TARGET_NAME)
    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "target_configure_minsizerel: Target '${TARGET_NAME}' does not exist")
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # MinSizeRel-specific compile options - optimize for size
        target_compile_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:MinSizeRel>:
                -Os                     # Optimize for size
                -ffunction-sections     # Place each function in its own section
                -fdata-sections         # Place each data item in its own section
                -fvisibility=hidden     # Hide symbols by default
                -fvisibility-inlines-hidden # Hide inline function symbols
            >
        )

        # MinSizeRel definitions
        target_compile_definitions(${TARGET_NAME} PRIVATE
            $<$<CONFIG:MinSizeRel>:
                NDEBUG
                ENGINE_RELEASE
            >
        )

        # Linker options for MinSizeRel
        target_link_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:MinSizeRel>:
                -Wl,--gc-sections       # Remove unused sections
                -Wl,--strip-all         # Strip all symbols
                -Wl,-s                  # Strip symbol table
            >
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:MinSizeRel>:
                /O1         # Minimize size
                /Ob1        # Inline expansion
                /Gy         # Function-level linking
            >
        )

        target_compile_definitions(${TARGET_NAME} PRIVATE
            $<$<CONFIG:MinSizeRel>:
                NDEBUG
                ENGINE_RELEASE
            >
        )

        target_link_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:MinSizeRel>:
                /OPT:REF    # Remove unreferenced code
                /OPT:ICF    # COMDAT folding
            >
        )
    endif()
endfunction()

# ==============================================================================
# Function to enable Link-Time Optimization (LTO) for Release builds
# ==============================================================================
function(target_enable_lto TARGET_NAME)
    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "target_enable_lto: Target '${TARGET_NAME}' does not exist")
    endif()

    include(CheckIPOSupported)
    check_ipo_supported(RESULT lto_supported OUTPUT lto_error)

    if(lto_supported)
        set_target_properties(${TARGET_NAME} PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
        )
        message(STATUS "LTO enabled for ${TARGET_NAME} (Release builds)")
    else()
        message(WARNING "LTO not supported for ${TARGET_NAME}: ${lto_error}")
    endif()
endfunction()

# ==============================================================================
# Function to enable sanitizers for Debug builds
# ==============================================================================
function(target_enable_sanitizers TARGET_NAME)
    set(options ADDRESS LEAK UNDEFINED THREAD MEMORY)
    set(oneValueArgs "")
    set(multiValueArgs "")
    cmake_parse_arguments(SANITIZER "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "target_enable_sanitizers: Target '${TARGET_NAME}' does not exist")
    endif()

    # MSVC doesn't support all sanitizers
    if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        if(SANITIZER_ADDRESS)
            target_compile_options(${TARGET_NAME} PRIVATE
                $<$<CONFIG:Debug>:/fsanitize=address>
            )
            message(STATUS "AddressSanitizer enabled for ${TARGET_NAME} (Debug builds)")
        endif()
        return()
    endif()

    # GCC/Clang sanitizer support
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(SANITIZER_FLAGS "")

        # AddressSanitizer (ASan) - detects memory errors
        if(SANITIZER_ADDRESS)
            list(APPEND SANITIZER_FLAGS "-fsanitize=address")
            message(STATUS "AddressSanitizer enabled for ${TARGET_NAME} (Debug builds)")
        endif()

        # LeakSanitizer (LSan) - detects memory leaks
        if(SANITIZER_LEAK)
            list(APPEND SANITIZER_FLAGS "-fsanitize=leak")
            message(STATUS "LeakSanitizer enabled for ${TARGET_NAME} (Debug builds)")
        endif()

        # UndefinedBehaviorSanitizer (UBSan) - detects undefined behavior
        if(SANITIZER_UNDEFINED)
            list(APPEND SANITIZER_FLAGS "-fsanitize=undefined")
            message(STATUS "UndefinedBehaviorSanitizer enabled for ${TARGET_NAME} (Debug builds)")
        endif()

        # ThreadSanitizer (TSan) - detects data races
        # Note: Cannot be used with ASan or LSan
        if(SANITIZER_THREAD)
            if(SANITIZER_ADDRESS OR SANITIZER_LEAK OR SANITIZER_MEMORY)
                message(WARNING "ThreadSanitizer cannot be combined with Address/Leak/Memory sanitizers")
            else()
                list(APPEND SANITIZER_FLAGS "-fsanitize=thread")
                message(STATUS "ThreadSanitizer enabled for ${TARGET_NAME} (Debug builds)")
            endif()
        endif()

        # MemorySanitizer (MSan) - detects uninitialized memory reads (Clang only)
        # Note: Cannot be used with ASan or TSan
        if(SANITIZER_MEMORY)
            if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
                message(WARNING "MemorySanitizer is only available with Clang")
            elseif(SANITIZER_ADDRESS OR SANITIZER_THREAD)
                message(WARNING "MemorySanitizer cannot be combined with Address/Thread sanitizers")
            else()
                list(APPEND SANITIZER_FLAGS "-fsanitize=memory")
                message(STATUS "MemorySanitizer enabled for ${TARGET_NAME} (Debug builds)")
            endif()
        endif()

        if(SANITIZER_FLAGS)
            # Apply sanitizer compile options
            target_compile_options(${TARGET_NAME} PRIVATE
                $<$<CONFIG:Debug>:${SANITIZER_FLAGS}>
            )

            # Apply sanitizer link options
            target_link_options(${TARGET_NAME} PRIVATE
                $<$<CONFIG:Debug>:${SANITIZER_FLAGS}>
            )
        endif()
    endif()
endfunction()

# ==============================================================================
# Function to apply all build configurations to a target
# ==============================================================================
function(target_configure_build TARGET_NAME)
    set(options ENABLE_LTO ENABLE_SANITIZERS)
    set(oneValueArgs "")
    set(multiValueArgs SANITIZERS)
    cmake_parse_arguments(CONFIG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "target_configure_build: Target '${TARGET_NAME}' does not exist")
    endif()

    # Apply Debug configuration
    target_configure_debug(${TARGET_NAME})

    # Apply Release configuration
    target_configure_release(${TARGET_NAME})

    # Apply RelWithDebInfo configuration
    target_configure_relwithdebinfo(${TARGET_NAME})

    # Apply MinSizeRel configuration
    target_configure_minsizerel(${TARGET_NAME})

    # Enable LTO if requested
    if(CONFIG_ENABLE_LTO)
        target_enable_lto(${TARGET_NAME})
    endif()

    # Enable sanitizers if requested
    if(CONFIG_ENABLE_SANITIZERS)
        if(CONFIG_SANITIZERS)
            # Convert list to function arguments
            set(SANITIZER_ARGS "")
            foreach(sanitizer ${CONFIG_SANITIZERS})
                string(TOUPPER "${sanitizer}" sanitizer_upper)
                list(APPEND SANITIZER_ARGS "${sanitizer_upper}")
            endforeach()
            target_enable_sanitizers(${TARGET_NAME} ${SANITIZER_ARGS})
        else()
            # Default sanitizers: Address and Undefined
            target_enable_sanitizers(${TARGET_NAME} ADDRESS UNDEFINED)
        endif()
    endif()
endfunction()

# ==============================================================================
# Global Build Configuration Options
# ==============================================================================
option(ENGINE_ENABLE_LTO "Enable Link-Time Optimization for Release builds" ON)
option(ENGINE_ENABLE_SANITIZERS "Enable sanitizers for Debug builds" ON)
option(ENGINE_SANITIZER_ADDRESS "Enable AddressSanitizer" ON)
option(ENGINE_SANITIZER_LEAK "Enable LeakSanitizer" ON)
option(ENGINE_SANITIZER_UNDEFINED "Enable UndefinedBehaviorSanitizer" ON)
option(ENGINE_SANITIZER_THREAD "Enable ThreadSanitizer (cannot be combined with ASan/LSan)" OFF)

# Release optimization options
option(ENGINE_OPTIMIZE_NATIVE "Optimize for current CPU (not suitable for distribution)" OFF)
option(ENGINE_FAST_MATH "Enable fast-math (breaks IEEE 754 compliance - use with caution)" OFF)

# ==============================================================================
# Setup Sanitizer Environment Variables
# ==============================================================================
if(ENGINE_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Create sanitizer options for runtime
    set(ASAN_OPTIONS "detect_leaks=1:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1")
    
    if(EXISTS "${LSAN_SUPPRESSION_FILE}")
        set(LSAN_OPTIONS "suppressions=${LSAN_SUPPRESSION_FILE}:print_suppressions=0")
        message(STATUS "Using LSan suppression file: ${LSAN_SUPPRESSION_FILE}")
    else()
        set(LSAN_OPTIONS "print_suppressions=0")
        message(STATUS "LSan suppression file not found, using defaults")
    endif()
    
    # Export for use in test/run scripts
    set(ENGINE_ASAN_OPTIONS "${ASAN_OPTIONS}" CACHE STRING "ASan runtime options")
    set(ENGINE_LSAN_OPTIONS "${LSAN_OPTIONS}" CACHE STRING "LSan runtime options")
endif()

# ==============================================================================
# Print Configuration Summary
# ==============================================================================
function(print_build_configuration)
    message(STATUS "")
    message(STATUS "========================================")
    message(STATUS " Build Configuration")
    message(STATUS "========================================")
    message(STATUS " Build Type: ${CMAKE_BUILD_TYPE}")
    message(STATUS " LTO Enabled: ${ENGINE_ENABLE_LTO}")
    message(STATUS " Native Optimization: ${ENGINE_OPTIMIZE_NATIVE}")
    message(STATUS " Fast Math: ${ENGINE_FAST_MATH}")
    message(STATUS " Sanitizers Enabled: ${ENGINE_ENABLE_SANITIZERS}")
    if(ENGINE_ENABLE_SANITIZERS)
        message(STATUS "   - AddressSanitizer: ${ENGINE_SANITIZER_ADDRESS}")
        message(STATUS "   - LeakSanitizer: ${ENGINE_SANITIZER_LEAK}")
        message(STATUS "   - UndefinedBehaviorSanitizer: ${ENGINE_SANITIZER_UNDEFINED}")
        message(STATUS "   - ThreadSanitizer: ${ENGINE_SANITIZER_THREAD}")
    endif()
    message(STATUS "========================================")
    message(STATUS "")
endfunction()
