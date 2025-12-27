# Cross-Compilation Guide

This project supports cross-compilation for Linux, Windows, and macOS targets.

## Quick Start

### Native Compilation (Default)

Build for your current operating system:

```bash
cmake -B build
cmake --build build
```

### Cross-Compilation

Specify the target OS with the `TARGET_OS` option:

```bash
# Build for Windows from Linux
cmake -B build-windows -DTARGET_OS=windows

# Build for macOS from Linux
cmake -B build-macos -DTARGET_OS=macos

# Build for Linux from macOS
cmake -B build-linux -DTARGET_OS=linux
```

## Requirements for Cross-Compilation

**Important:** Cross-compilation requires installing the appropriate toolchain for your target platform. The build will fail with a clear error message if the required toolchain is not found.

### Linux → Windows

**Required:** Install MinGW-w64 cross-compiler before attempting to build:

```bash
# Ubuntu/Debian
sudo apt-get install mingw-w64

# Fedora
sudo dnf install mingw64-gcc mingw64-gcc-c++

# Arch Linux
sudo pacman -S mingw-w64-gcc
```

Verify installation:
```bash
which x86_64-w64-mingw32-gcc x86_64-w64-mingw32-g++
```

### Linux → macOS

**Required:** OSXCross toolchain. See: https://github.com/tpoechtrager/osxcross

This is a complex setup requiring Xcode SDK. Follow the OSXCross documentation carefully.

### macOS → Windows

Requires MinGW-w64 cross-compiler via Homebrew:

```bash
brew install mingw-w64
```

### Windows → Linux

Requires WSL2 or a Linux cross-compiler toolchain.

## Using Custom Toolchain Files

For advanced cross-compilation scenarios, you can provide a custom toolchain file:

```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain.cmake
```

Example toolchain file for Windows cross-compilation on Linux:

```cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

## Output Directory Structure

Binaries are organized by target OS to prevent mixing:

```
bin/
├── linux/      # Linux binaries
├── windows/    # Windows binaries (.exe, .dll)
└── macos/      # macOS binaries (.dylib)
```

## Notes

- The project automatically downloads platform-specific tools (Slang compiler, Wasmtime runtime) for the target OS.
- When cross-compiling, ensure all dependencies are available for the target platform.
- Some dependencies may require manual installation for cross-compilation scenarios.

## Troubleshooting

### "Cross-compiling to Windows requires MinGW-w64 toolchain" Error

This error occurs when trying to build for Windows without the MinGW-w64 cross-compiler installed.

**Solution:**
1. Install MinGW-w64 (see requirements above)
2. Or provide a custom toolchain file
3. Or set compilers manually:
   ```bash
   cmake -B build -DTARGET_OS=windows \
     -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
     -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++
   ```

### Linker Errors When Cross-Compiling

If you see errors like `unrecognized option '--major-image-version'`, this means the wrong compiler is being used (e.g., native GCC instead of MinGW).

**Solution:**
1. Ensure MinGW is installed and in your PATH
2. Clean the build directory: `rm -rf build`
3. Reconfigure with: `cmake -B build -DTARGET_OS=windows`

### Dependencies Not Found During Cross-Compilation

Some dependencies (like glfw3, Vulkan) may not be available for the target platform.

**Solution:**
1. Install cross-compiled versions of dependencies
2. Or use a toolchain file that specifies where to find them
3. Or disable features that require unavailable dependencies
