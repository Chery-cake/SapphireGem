# Cross-compilation setup
# This file configures CMake for cross-compilation based on TARGET_OS

if(NOT DEFINED TARGET_OS)
    message(FATAL_ERROR "TARGET_OS must be defined before including SetupCrossCompilation.cmake")
endif()

# Determine if we're actually cross-compiling
# Compare TARGET_OS with the host system
set(CROSS_COMPILING FALSE)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND NOT TARGET_OS STREQUAL "linux")
    set(CROSS_COMPILING TRUE)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows" AND NOT TARGET_OS STREQUAL "windows")
    set(CROSS_COMPILING TRUE)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND NOT TARGET_OS STREQUAL "macos")
    set(CROSS_COMPILING TRUE)
endif()

if(CROSS_COMPILING)
    # Set CMAKE_SYSTEM_NAME based on TARGET_OS
    if(TARGET_OS STREQUAL "windows")
        set(CMAKE_SYSTEM_NAME Windows)
        set(CMAKE_SYSTEM_PROCESSOR x86_64)
        
        # For cross-compilation, we need to force the use of MinGW compilers
        # Find MinGW cross-compiler
        find_program(MINGW_CC x86_64-w64-mingw32-gcc)
        find_program(MINGW_CXX x86_64-w64-mingw32-g++)
        
        if(NOT MINGW_CC OR NOT MINGW_CXX)
            message(FATAL_ERROR 
                "Cross-compiling to Windows requires MinGW-w64 toolchain.\n"
                "Please install it:\n"
                "  Ubuntu/Debian: sudo apt-get install mingw-w64\n"
                "  Fedora: sudo dnf install mingw64-gcc mingw64-gcc-c++\n"
                "  Arch: sudo pacman -S mingw-w64-gcc\n"
                "\n"
                "After installation, verify with: which x86_64-w64-mingw32-gcc\n"
                "\n"
                "Alternatively:\n"
                "  - Use a toolchain file: cmake -DCMAKE_TOOLCHAIN_FILE=path/to/toolchain.cmake\n"
                "  - Set compilers manually: cmake -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++")
        endif()
        
        # Force set the compilers for cross-compilation
        set(CMAKE_C_COMPILER ${MINGW_CC} CACHE FILEPATH "C compiler" FORCE)
        set(CMAKE_CXX_COMPILER ${MINGW_CXX} CACHE FILEPATH "C++ compiler" FORCE)
        
        message(STATUS "Using MinGW compilers:")
        message(STATUS "  C compiler: ${CMAKE_C_COMPILER}")
        message(STATUS "  C++ compiler: ${CMAKE_CXX_COMPILER}")
        
        # Set find root paths for cross-compilation
        set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
        set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
        set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
        set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
        
    elseif(TARGET_OS STREQUAL "linux")
        set(CMAKE_SYSTEM_NAME Linux)
        set(CMAKE_SYSTEM_PROCESSOR x86_64)
        
    elseif(TARGET_OS STREQUAL "macos")
        set(CMAKE_SYSTEM_NAME Darwin)
        set(CMAKE_SYSTEM_PROCESSOR x86_64)
        
        # macOS cross-compilation requires OSXCross or similar toolchain
        message(FATAL_ERROR 
            "Cross-compiling to macOS requires OSXCross toolchain.\n"
            "See: https://github.com/tpoechtrager/osxcross\n"
            "Or provide a toolchain file with: cmake -DCMAKE_TOOLCHAIN_FILE=path/to/toolchain.cmake")
    endif()
    
    message(STATUS "Cross-compilation enabled: ${CMAKE_HOST_SYSTEM_NAME} -> ${TARGET_OS}")
else()
    message(STATUS "Native compilation for ${TARGET_OS}")
endif()

# Set platform-specific file extensions
if(TARGET_OS STREQUAL "windows")
    set(PLATFORM_EXECUTABLE_SUFFIX ".exe")
    set(PLATFORM_SHARED_LIBRARY_PREFIX "")
    set(PLATFORM_SHARED_LIBRARY_SUFFIX ".dll")
    set(PLATFORM_STATIC_LIBRARY_PREFIX "")
    set(PLATFORM_STATIC_LIBRARY_SUFFIX ".lib")
elseif(TARGET_OS STREQUAL "macos")
    set(PLATFORM_EXECUTABLE_SUFFIX "")
    set(PLATFORM_SHARED_LIBRARY_PREFIX "lib")
    set(PLATFORM_SHARED_LIBRARY_SUFFIX ".dylib")
    set(PLATFORM_STATIC_LIBRARY_PREFIX "lib")
    set(PLATFORM_STATIC_LIBRARY_SUFFIX ".a")
else() # Linux and others
    set(PLATFORM_EXECUTABLE_SUFFIX "")
    set(PLATFORM_SHARED_LIBRARY_PREFIX "lib")
    set(PLATFORM_SHARED_LIBRARY_SUFFIX ".so")
    set(PLATFORM_STATIC_LIBRARY_PREFIX "lib")
    set(PLATFORM_STATIC_LIBRARY_SUFFIX ".a")
endif()
