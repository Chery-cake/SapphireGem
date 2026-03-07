# ==============================================================================
# Utility Dependencies
# ==============================================================================
# - nlohmann/json - Modern JSON for C++
# - assimp - Universal 3D model loader
# - zlib - Compression
# - PhysicsFS - Virtual file system
# - recast/detour - Navigation meshes
# ==============================================================================

include(FetchContent)

# ==============================================================================
# nlohmann/json - Header-Only JSON Library
# ==============================================================================
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# ==============================================================================
# zlib - Compression Library
# ==============================================================================

# SDL_image (loaded via lib/graphics/) already provides zlib targets.
# Only fetch standalone zlib if no zlib target exists yet.
if(NOT TARGET zlib)
    FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3.1.2
        GIT_SHALLOW TRUE
    )
    set(ZLIB_BUILD_STATIC OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(zlib)
endif()

# ==============================================================================
# Assimp - Asset Import Library
# ==============================================================================
FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG v6.0.2
    GIT_SHALLOW TRUE
)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)

# Pre-set ZLIB info for assimp from the existing zlib target (from SDL_image).
# On Windows, find_package(ZLIB) fails because there's no system zlib.
if(TARGET zlib)
    set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB TRUE CACHE BOOL "" FORCE)
    set(ZLIB_FOUND TRUE CACHE BOOL "" FORCE)
    set(ZLIB_LIBRARIES zlib CACHE STRING "" FORCE)
    get_target_property(_zlib_inc zlib INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT _zlib_inc OR _zlib_inc STREQUAL "_zlib_inc-NOTFOUND")
        get_target_property(_zlib_inc zlib INTERFACE_SYSTEM_INCLUDE_DIRECTORIES)
    endif()
    if(NOT _zlib_inc OR _zlib_inc STREQUAL "_zlib_inc-NOTFOUND")
        get_target_property(_zlib_inc zlib SOURCE_DIR)
    endif()
    set(ZLIB_INCLUDE_DIR "${_zlib_inc}" CACHE PATH "" FORCE)
endif()

FetchContent_MakeAvailable(assimp)

# ==============================================================================
# PhysicsFS - Virtual Filesystem
# ==============================================================================
FetchContent_Declare(
    physfs
    GIT_REPOSITORY https://github.com/icculus/physfs.git
    GIT_TAG stable-3.2
    GIT_SHALLOW TRUE
)
set(PHYSFS_BUILD_STATIC OFF CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_SHARED ON CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(physfs)
set(CMAKE_SKIP_RPATH OFF CACHE BOOL "Skip RPATH" FORCE)

# ==============================================================================
# Recast & Detour - Navigation Meshes
# ==============================================================================
FetchContent_Declare(
    recastnavigation
    GIT_REPOSITORY https://github.com/recastnavigation/recastnavigation.git
    GIT_TAG v1.6.0
    GIT_SHALLOW TRUE
)
set(RECASTNAVIGATION_DEMO OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_TESTS OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(recastnavigation)

message(STATUS "Utility dependencies configured")
