# ==============================================================================
# Dependencies - Main Configuration
# ==============================================================================
# Manages all external dependencies using FetchContent where feasible.
# Organized by subsystem: graphics, audio, physics, math, threading, ecs, etc.
# ==============================================================================

include(FetchContent)

# Set FetchContent options
set(FETCHCONTENT_QUIET OFF)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# ==============================================================================
# Include Subdirectory Configurations
# ==============================================================================
add_subdirectory(graphics)
#add_subdirectory(audio)
#add_subdirectory(physics)
add_subdirectory(math)
add_subdirectory(threading)
#add_subdirectory(memory)
#add_subdirectory(scripting)
#add_subdirectory(networking)
add_subdirectory(utility)

message(STATUS "Dependencies configured successfully")
