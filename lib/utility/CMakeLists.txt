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
set(ASSIMP_BUILD_USE_CCACHE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)

if(WIN32)
  set(ASSIMP_BUILD_ZLIB OFF CACHE BOOL "" FORCE)
  set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB TRUE)
    set(ZLIB_FOUND TRUE)
    set(ZLIB_LIBRARIES zlibstatic)
    set(ZLIB_INCLUDE_DIR
        "${sdl_image_SOURCE_DIR}/external/zlib"
        "${sdl_image_BINARY_DIR}/external/zlib"
    )
endif()

FetchContent_MakeAvailable(assimp)

# ==============================================================================
# zlib - Compression Library
# ==============================================================================

#FetchContent_Declare(
#    zlib
#    GIT_REPOSITORY https://github.com/madler/zlib.git
#    GIT_TAG v1.3.1.2
#    GIT_SHALLOW TRUE
#)
#set(ZLIB_BUILD_STATIC OFF CACHE BOOL "" FORCE)
#set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
#FetchContent_MakeAvailable(zlib)

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
