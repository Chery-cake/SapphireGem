# ==============================================================================
# Threading Dependencies
# ==============================================================================
# - bshoshany/thread-pool - Modern C++ thread pool
# ==============================================================================

include(FetchContent)

find_package(TBB REQUIRED)

# Create an interface library to abstract the actual TBB target name
add_library(tbb_engine INTERFACE)

if(TARGET TBB::tbb)
    target_link_libraries(tbb_engine INTERFACE TBB::tbb)
elseif(TARGET tbb)
    target_link_libraries(tbb_engine INTERFACE tbb)
else()
    # Fallback for older FindTBB.cmake that sets TBB_LIBRARIES
    target_link_libraries(tbb_engine INTERFACE ${TBB_LIBRARIES})
    target_include_directories(tbb_engine INTERFACE ${TBB_INCLUDE_DIRS})
endif()

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
