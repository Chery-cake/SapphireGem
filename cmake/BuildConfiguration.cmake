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
# Function to configure sanitizer suppressions for a target
# ==============================================================================
# This function:
# 1. Configures sanitizer options with suppression file paths
# 2. Generates TWO launcher scripts:
#    - run_<target>.sh: ASan + LSan + UBSan via LD_PRELOAD
#    - run_<target>_tsan.sh: TSan via LD_PRELOAD
# 3. Configures ctest to use the same suppression files
# ==============================================================================
function(target_enable_sanitizers TARGET_NAME)
    set(options ADDRESS LEAK UNDEFINED THREAD MEMORY)
    set(oneValueArgs "")
    set(multiValueArgs "")
    cmake_parse_arguments(SANITIZER "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "target_enable_sanitizers: Target '${TARGET_NAME}' does not exist")
    endif()

    # Get the output directory for the target
    get_target_property(TARGET_RUNTIME_DIR ${TARGET_NAME} RUNTIME_OUTPUT_DIRECTORY)
    if(NOT TARGET_RUNTIME_DIR)
        set(TARGET_RUNTIME_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    endif()

    # MSVC doesn't support all sanitizers
    if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        if(SANITIZER_ADDRESS)
            target_compile_options(${TARGET_NAME} PRIVATE
                $<$<CONFIG:Debug>:/fsanitize=address>
            )
            message(STATUS "AddressSanitizer enabled for ${TARGET_NAME} (Debug builds)")
        endif()
        if(SANITIZER_LEAK)
            message(WARNING "LeakSanitizer is not supported by MSVC - skipping")
        endif()
        if(SANITIZER_UNDEFINED)
            message(WARNING "UndefinedBehaviorSanitizer is not supported by MSVC - skipping")
        endif()
        if(SANITIZER_THREAD)
            message(WARNING "ThreadSanitizer is not supported by MSVC - skipping")
        endif()
        if(SANITIZER_MEMORY)
            message(WARNING "MemorySanitizer is not supported by MSVC - skipping")
        endif()
        return()
    endif()

    # Log which sanitizers are enabled (applied via launcher scripts, not compile/link flags)
    # ==========================================================================
    # Find sanitizer shared library paths (GCC/Clang only)
    # ==========================================================================
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      # Determine preload environment variable and library extension (platform-specific)
        if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            set(PRELOAD_VAR "DYLD_INSERT_LIBRARIES")
            set(_SAN_LIB_EXT "dylib")
        else()
            set(PRELOAD_VAR "LD_PRELOAD")
            set(_SAN_LIB_EXT "so")
        endif()

      set(SANITIZER_EXPORT "")
      
        # AddressSanitizer (ASan) - detects memory errors
        if(SANITIZER_ADDRESS)
    # Build sanitizer options with suppression file paths (using original source files)
    set(ASAN_OPTIONS "detect_leaks=1:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1")
    if(EXISTS "${ASAN_SUPPRESSION_FILE}")
        set(ASAN_OPTIONS "${ASAN_OPTIONS}:suppressions=${ASAN_SUPPRESSION_FILE}")
        message(STATUS "Using ASan suppression file: ${ASAN_SUPPRESSION_FILE}")
    endif()
    
set(SANITIZER_EXPORT "export ASAN_OPTIONS=\"${ASAN_OPTIONS}\"")

        # Find ASan shared library
        execute_process(
            COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libasan.${_SAN_LIB_EXT}
            OUTPUT_VARIABLE _ASAN_LIB OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
        )
        if(NOT IS_ABSOLUTE "${_ASAN_LIB}")
            set(_ASAN_LIB "")
        endif()

            message(STATUS "AddressSanitizer enabled for ${TARGET_NAME} (via launcher script)")
        endif()

        # LeakSanitizer (LSan) - detects memory leaks
        if(SANITIZER_LEAK)
# Build sanitizer options with suppression file paths (using original source files)
if(EXISTS "${LSAN_SUPPRESSION_FILE}")
        set(LSAN_OPTIONS "suppressions=${LSAN_SUPPRESSION_FILE}:print_suppressions=0")
        message(STATUS "Using LSan suppression file: ${LSAN_SUPPRESSION_FILE}")
    else()
        set(LSAN_OPTIONS "print_suppressions=0")
        message(STATUS "LSan suppression file not found, using defaults")
    endif()
set(SANITIZER_EXPORT "${SANITIZER_EXPORT}\nexport LSAN_OPTIONS=\"${LSAN_OPTIONS}\"")
            message(STATUS "LeakSanitizer enabled for ${TARGET_NAME} (via launcher script)")
        endif()

        # UndefinedBehaviorSanitizer (UBSan) - detects undefined behavior
        if(SANITIZER_UNDEFINED)
          set(SANITIZER_EXPORT "${SANITIZER_EXPORT}\nexport UBSAN_OPTIONS=\"print_stacktrace=1\"")
          
        # Find UBSan shared library
        execute_process(
            COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libubsan.${_SAN_LIB_EXT}
            OUTPUT_VARIABLE _UBSAN_LIB OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
        )
        if(NOT IS_ABSOLUTE "${_UBSAN_LIB}")
            set(_UBSAN_LIB "")
        endif()
        
            message(STATUS "UndefinedBehaviorSanitizer enabled for ${TARGET_NAME} (via launcher script)")
        endif()

        # ThreadSanitizer (TSan) - detects data races
        # Note: Cannot be used with ASan or LSan
        if(SANITIZER_THREAD)
          # Find TSan shared library
        execute_process(
            COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libtsan.${_SAN_LIB_EXT}
            OUTPUT_VARIABLE _TSAN_LIB OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
        )
        if(NOT IS_ABSOLUTE "${_TSAN_LIB}")
            set(_TSAN_LIB "")
        endif()
          
            message(STATUS "ThreadSanitizer enabled for ${TARGET_NAME} (via launcher script)")
        endif()

        # MemorySanitizer (MSan) - detects uninitialized memory reads (Clang only)
        # Note: Cannot be used with ASan or TSan
        # Check to add MSan to the scripts or create a new one for it
        if(SANITIZER_MEMORY)
            if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
                message(WARNING "MemorySanitizer is only available with Clang")
            elseif(SANITIZER_ADDRESS OR SANITIZER_THREAD)
                message(WARNING "MemorySanitizer cannot be combined with Address/Thread sanitizers")
            else()
                message(STATUS "MemorySanitizer enabled for ${TARGET_NAME} (via launcher script)")
            endif()
        endif()

# Build LD_PRELOAD string for ASan + UBSan
        set(SANITIZER_PRELOAD "")
        if(_ASAN_LIB)
            set(SANITIZER_PRELOAD "${_ASAN_LIB}")
        endif()
        if(_UBSAN_LIB)
            if(SANITIZER_PRELOAD)
                set(SANITIZER_PRELOAD "${SANITIZER_PRELOAD}:${_UBSAN_LIB}")
            else()
                set(SANITIZER_PRELOAD "${_UBSAN_LIB}")
            endif()
        endif()

        # Build LD_PRELOAD string for TSan
        set(TSAN_PRELOAD "")
        if(_TSAN_LIB)
            set(TSAN_PRELOAD "${_TSAN_LIB}")
        endif()
    endif()

    if( SANITIZER_ADDRESS OR SANITIZER_LEAK OR SANITIZER_UNDEFINED )
          # ==========================================================================
    # Script 1: ASan + LSan + UBSan launcher
    # ==========================================================================
    set(LAUNCHER_SCRIPT "${TARGET_RUNTIME_DIR}/run_${TARGET_NAME}.sh")
    file(WRITE "${LAUNCHER_SCRIPT}"
"#!/bin/bash
# ==============================================================================
# Launcher script for ${TARGET_NAME} with ASan + LSan + UBSan
# Generated by CMake - DO NOT EDIT
# ==============================================================================

SCRIPT_DIR=\"$(cd \"$(dirname \"\${BASH_SOURCE[0]}\")\" && pwd)\"

# Set sanitizer options (configured by CMake)
${SANITIZER_EXPORT}

# Preload sanitizer runtimes to avoid library ordering issues
if [ -n \"${SANITIZER_PRELOAD}\" ]; then
    export ${PRELOAD_VAR}=\"${SANITIZER_PRELOAD}\"
fi

exec \"\${SCRIPT_DIR}/${TARGET_NAME}\" \"$@\"
")
    file(CHMOD "${LAUNCHER_SCRIPT}"
        FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                         GROUP_READ GROUP_EXECUTE
                         WORLD_READ WORLD_EXECUTE
    )
    message(STATUS "Generated ASan/LSan/UBSan launcher script: ${LAUNCHER_SCRIPT}")
      endif()

      if( SANITIZER_THREAD )
    # ==========================================================================
    # Script 2: TSan (ThreadSanitizer) launcher
    # ==========================================================================
    set(TSAN_SCRIPT "${TARGET_RUNTIME_DIR}/run_${TARGET_NAME}_tsan.sh")
    file(WRITE "${TSAN_SCRIPT}"
"#!/bin/bash
# ==============================================================================
# Launcher script for ${TARGET_NAME} with ThreadSanitizer
# Generated by CMake - DO NOT EDIT
# ==============================================================================

SCRIPT_DIR=\"$(cd \"$(dirname \"\${BASH_SOURCE[0]}\")\" && pwd)\"

# Set ThreadSanitizer options
export TSAN_OPTIONS=\"history_size=7:second_deadlock_stack=1\"

# Preload TSan runtime to avoid library ordering issues
if [ -n \"${TSAN_PRELOAD}\" ]; then
    export ${PRELOAD_VAR}=\"${TSAN_PRELOAD}\"
fi

exec \"\${SCRIPT_DIR}/${TARGET_NAME}\" \"$@\"
")
    file(CHMOD "${TSAN_SCRIPT}"
        FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                         GROUP_READ GROUP_EXECUTE
                         WORLD_READ WORLD_EXECUTE
    )
    message(STATUS "Generated TSan launcher script: ${TSAN_SCRIPT}")
        endif()
      message(STATUS "Sanitizer suppressions configured for ${TARGET_NAME}")
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
            # Default sanitizers: Address
            target_enable_sanitizers(${TARGET_NAME} ADDRESS)
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
