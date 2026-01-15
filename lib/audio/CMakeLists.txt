# ==============================================================================
# Audio Dependencies
# ==============================================================================
# - OpenAL Soft - 3D/multichannel audio
# ==============================================================================

include(FetchContent)

# ==============================================================================
# OpenAL Soft - 3D Spatial Audio
# ==============================================================================
FetchContent_Declare(
    openal
    GIT_REPOSITORY https://github.com/kcat/openal-soft.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL OFF CACHE BOOL "" FORCE)
set(LIBTYPE SHARED)
FetchContent_MakeAvailable(openal)

message(STATUS "Audio dependencies configured")
