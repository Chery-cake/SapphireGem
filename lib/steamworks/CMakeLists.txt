project(steamworks)

add_library(${PROJECT_NAME} SHARED IMPORTED GLOBAL)

set_target_properties(${PROJECT_NAME} PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/public"
)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(STEAM_LIB_FILE "${CMAKE_CURRENT_SOURCE_DIR}/redistributable_bin/win64/steam_api64.lib")
        set(STEAM_DLL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/redistributable_bin/win64/steam_api64.dll")
    else()
        set(STEAM_LIB_FILE "${CMAKE_CURRENT_SOURCE_DIR}/redistributable_bin/steam_api.lib")
        set(STEAM_DLL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/redistributable_bin/steam_api.dll")
    endif()

    set_target_properties(${PROJECT_NAME} PROPERTIES
        IMPORTED_IMPLIB "${STEAM_LIB_FILE}"
        IMPORTED_LOCATION "${STEAM_DLL_FILE}"
    )

elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(STEAM_LIB_FILE "${CMAKE_CURRENT_SOURCE_DIR}/redistributable_bin/linux64/libsteam_api.so")
    else()
        set(STEAM_LIB_FILE "${CMAKE_CURRENT_SOURCE_DIR}/redistributable_bin/linux32/libsteam_api.so")
    endif()

    set_target_properties(${PROJECT_NAME} PROPERTIES
        IMPORTED_LOCATION "${STEAM_LIB_FILE}"
    )

endif()

# Verify files exist
if(NOT EXISTS "${STEAM_LIB_FILE}")
    message(FATAL_ERROR "Steam library not found at: ${STEAM_LIB_FILE}")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND NOT EXISTS "${STEAM_DLL_FILE}")
    message(FATAL_ERROR "Steam DLL not found at: ${STEAM_DLL_FILE}")
endif()

message(STATUS "Using Steam library: ${STEAM_LIB_FILE}")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(STATUS "Using Steam DLL: ${STEAM_DLL_FILE}")
endif()

# Set the binary file to copy based on platform
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(STEAM_BIN_FILE "${STEAM_DLL_FILE}")
else()
    set(STEAM_BIN_FILE "${STEAM_LIB_FILE}")
endif()

# Get the common output directory from the parent scope
get_directory_property(PARENT_DIR PARENT_DIRECTORY)
get_property(COMMON_OUTPUT_DIR DIRECTORY ${PARENT_DIR} PROPERTY COMMON_OUTPUT_DIR)

# If not found, set a default
if(NOT COMMON_OUTPUT_DIR)
    set(COMMON_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/../bin")
endif()

# Create custom target to copy Steamworks binaries to common output directory
add_custom_target(copy_steam ALL
    COMMAND ${CMAKE_COMMAND} -E echo "Copying Steamworks binaries to ${COMMON_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${COMMON_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${STEAM_BIN_FILE}"
        "${COMMON_OUTPUT_DIR}/"
    COMMENT "Copying Steamworks binaries to output directory"
)

# Make the copy_steam target depend on the imported library
add_dependencies(copy_steam ${PROJECT_NAME})
