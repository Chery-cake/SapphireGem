# Download and setup Slang compiler for shader compilation

set(SLANG_FALLBACK_VERSION "2025.24.2")
set(SLANG_VERSION ${SLANG_FALLBACK_VERSION})

# Try to fetch the latest release information from GitHub API
message(STATUS "Attempting to fetch latest Slang release...")
file(DOWNLOAD
    "https://api.github.com/repos/shader-slang/slang/releases/latest"
    "${CMAKE_BINARY_DIR}/slang_latest_release.json"
    TIMEOUT 10
    STATUS API_DOWNLOAD_STATUS
    LOG API_DOWNLOAD_LOG
)

list(GET API_DOWNLOAD_STATUS 0 API_ERROR_CODE)
if(API_ERROR_CODE EQUAL 0 AND EXISTS "${CMAKE_BINARY_DIR}/slang_latest_release.json")
    file(READ "${CMAKE_BINARY_DIR}/slang_latest_release.json" SLANG_RELEASE_JSON)
    # Try to parse the tag_name from JSON
    string(JSON SLANG_TAG_NAME ERROR_VARIABLE JSON_ERROR GET ${SLANG_RELEASE_JSON} tag_name)
    if(NOT JSON_ERROR AND SLANG_TAG_NAME)
        # Remove 'v' prefix if present
        string(REPLACE "v" "" SLANG_VERSION ${SLANG_TAG_NAME})
        message(STATUS "Found latest Slang release: ${SLANG_VERSION}")
    else()
        message(STATUS "Could not parse release info, using fallback version ${SLANG_FALLBACK_VERSION}")
    endif()
else()
    message(STATUS "Could not fetch latest release info (API may be blocked), using fallback version ${SLANG_FALLBACK_VERSION}")
endif()

# Download platform-specific Slang compiler package
set(SLANG_DIR ${CMAKE_BINARY_DIR}/slang)

# Determine platform based on TARGET_OS
if(TARGET_OS STREQUAL "windows")
    set(SLANG_PLATFORM "windows-x86_64")
    set(SLANG_ARCHIVE "slang-${SLANG_VERSION}-${SLANG_PLATFORM}.zip")
    set(SLANG_COMPILER "${SLANG_DIR}/bin/slangc.exe")
elseif(TARGET_OS STREQUAL "macos")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(SLANG_PLATFORM "macos-aarch64")
    else()
        set(SLANG_PLATFORM "macos-x86_64")
    endif()
    set(SLANG_ARCHIVE "slang-${SLANG_VERSION}-${SLANG_PLATFORM}.zip")
    set(SLANG_COMPILER "${SLANG_DIR}/bin/slangc")
else() # linux
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(SLANG_PLATFORM "linux-aarch64")
    else()
        set(SLANG_PLATFORM "linux-x86_64")
    endif()
    set(SLANG_ARCHIVE "slang-${SLANG_VERSION}-${SLANG_PLATFORM}.tar.gz")
    set(SLANG_COMPILER "${SLANG_DIR}/bin/slangc")
endif()

set(SLANG_URL "https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/${SLANG_ARCHIVE}")

if(NOT EXISTS ${SLANG_COMPILER})
    message(STATUS "Downloading Slang ${SLANG_VERSION} for ${SLANG_PLATFORM}...")
    message(STATUS "URL: ${SLANG_URL}")
    file(DOWNLOAD ${SLANG_URL} ${CMAKE_BINARY_DIR}/${SLANG_ARCHIVE}
        SHOW_PROGRESS
        STATUS SLANG_DOWNLOAD_STATUS)

    list(GET SLANG_DOWNLOAD_STATUS 0 SLANG_ERROR)
    if(SLANG_ERROR)
        message(FATAL_ERROR "Failed to download Slang from ${SLANG_URL}")
    endif()

    message(STATUS "Extracting Slang to ${SLANG_DIR}")
    file(ARCHIVE_EXTRACT
        INPUT ${CMAKE_BINARY_DIR}/${SLANG_ARCHIVE}
        DESTINATION ${SLANG_DIR})
endif()

message(STATUS "Slang compiler: ${SLANG_COMPILER}")

# Export variables for use in parent scope
set(SLANG_COMPILER ${SLANG_COMPILER} PARENT_SCOPE)
set(SLANG_DIR ${SLANG_DIR} PARENT_SCOPE)
