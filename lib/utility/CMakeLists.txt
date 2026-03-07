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

# Expose the existing zlib target to assimp's find_package(ZLIB) call.
# On Windows there is no system zlib, so FindZLIB.cmake fails even though
# the zlib CMake target (from SDL_image or standalone FetchContent) exists.
# We set the ZLIB_ variables and disable find_package(ZLIB) so assimp picks
# up the already-configured target instead of searching for system files.
if(TARGET zlib AND NOT ZLIB_FOUND)
    get_target_property(_zlib_source_dir zlib SOURCE_DIR)
    get_target_property(_zlib_binary_dir zlib BINARY_DIR)
    set(ZLIB_FOUND TRUE)
    set(ZLIB_INCLUDE_DIR "${_zlib_source_dir}" "${_zlib_binary_dir}")
    set(ZLIB_LIBRARIES zlib)
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
set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB TRUE)
FetchContent_MakeAvailable(assimp)
unset(CMAKE_DISABLE_FIND_PACKAGE_ZLIB)

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
