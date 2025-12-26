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

### Linux → Windows

Install MinGW-w64 cross-compiler:

```bash
# Ubuntu/Debian
sudo apt-get install mingw-w64

# Fedora
sudo dnf install mingw64-gcc mingw64-gcc-c++

# Arch Linux
sudo pacman -S mingw-w64-gcc
```

### Linux → macOS

Requires OSXCross toolchain. See: https://github.com/tpoechtrager/osxcross

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
