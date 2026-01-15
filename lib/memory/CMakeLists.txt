# ==============================================================================
# Memory Management Dependencies
# ==============================================================================
# - chintan-27/BumpAllocator - Memory allocation
# - EnTT - Entity Component System (ecs)
# ==============================================================================

include(FetchContent)

# ==============================================================================
# chintan-27/BumpAllocator - Memory allocation
# ==============================================================================
#FetchContent_Declare(
#    bump_allocator
#    GIT_REPOSITORY https://github.com/chintan-27/BumpAllocator.git
#    GIT_TAG 2a70d683ad149abe18963e115f09a1d08b2e0f33 # ThreadSafe Allocator Implemented
#    GIT_SHALLOW TRUE
#)
#FetchContent_MakeAvailable(bump_allocator)

#add_library(bump_allocator INTERFACE)
#target_include_directories(bump_allocator INTERFACE ${bump_allocator_SOURCE_DIR}/include)

# ==============================================================================
# EnTT - Entity Component System (ecs)
# ==============================================================================
FetchContent_Declare(
    entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v3.16.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(entt)

message(STATUS "Memory Management dependencies configured")
