# ==============================================================================
# Threading Dependencies
# ==============================================================================
# - bshoshany/thread-pool - Modern C++ thread pool
# ==============================================================================

include(FetchContent)

# ==============================================================================
# BS::thread_pool - Modern C++ Thread Pool
# ==============================================================================
FetchContent_Declare(
    thread_pool
    GIT_REPOSITORY https://github.com/bshoshany/thread-pool.git
    GIT_TAG v5.1.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(thread_pool)

add_library(thread_pool INTERFACE)
target_include_directories(thread_pool INTERFACE ${thread_pool_SOURCE_DIR}/include)

message(STATUS "Threading dependencies configured")
