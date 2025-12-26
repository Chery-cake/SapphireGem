# Download and setup Wasmtime runtime for WASM execution

set(WASMTIME_VERSION "28.0.0")
set(WASMTIME_DIR ${CMAKE_BINARY_DIR}/wasmtime)

# Determine platform and architecture
if(TARGET_OS STREQUAL "windows")
    set(WASMTIME_PLATFORM "x86_64-windows")
    set(WASMTIME_ARCHIVE "wasmtime-v${WASMTIME_VERSION}-${WASMTIME_PLATFORM}-c-api.zip")
    set(WASMTIME_EXTRACTED_DIR "${WASMTIME_DIR}/wasmtime-v${WASMTIME_VERSION}-${WASMTIME_PLATFORM}-c-api")
    set(WASMTIME_LIBRARY "${WASMTIME_EXTRACTED_DIR}/lib/wasmtime.dll.lib")
    set(WASMTIME_DLL "${WASMTIME_EXTRACTED_DIR}/bin/wasmtime.dll")
elseif(TARGET_OS STREQUAL "macos")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(WASMTIME_PLATFORM "aarch64-macos")
    else()
        set(WASMTIME_PLATFORM "x86_64-macos")
    endif()
    set(WASMTIME_ARCHIVE "wasmtime-v${WASMTIME_VERSION}-${WASMTIME_PLATFORM}-c-api.tar.xz")
    set(WASMTIME_EXTRACTED_DIR "${WASMTIME_DIR}/wasmtime-v${WASMTIME_VERSION}-${WASMTIME_PLATFORM}-c-api")
    set(WASMTIME_LIBRARY "${WASMTIME_EXTRACTED_DIR}/lib/libwasmtime.dylib")
    set(WASMTIME_DLL "${WASMTIME_LIBRARY}")
else() # linux
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        set(WASMTIME_PLATFORM "aarch64-linux")
    else()
        set(WASMTIME_PLATFORM "x86_64-linux")
    endif()
    set(WASMTIME_ARCHIVE "wasmtime-v${WASMTIME_VERSION}-${WASMTIME_PLATFORM}-c-api.tar.xz")
    set(WASMTIME_EXTRACTED_DIR "${WASMTIME_DIR}/wasmtime-v${WASMTIME_VERSION}-${WASMTIME_PLATFORM}-c-api")
    set(WASMTIME_LIBRARY "${WASMTIME_EXTRACTED_DIR}/lib/libwasmtime.so")
    set(WASMTIME_DLL "${WASMTIME_LIBRARY}")
endif()

set(WASMTIME_URL "https://github.com/bytecodealliance/wasmtime/releases/download/v${WASMTIME_VERSION}/${WASMTIME_ARCHIVE}")
set(WASMTIME_INCLUDE_DIR "${WASMTIME_EXTRACTED_DIR}/include")

# Download and extract Wasmtime if not already present
if(NOT EXISTS ${WASMTIME_LIBRARY})
    message(STATUS "Downloading Wasmtime ${WASMTIME_VERSION} for ${WASMTIME_PLATFORM}...")
    file(DOWNLOAD ${WASMTIME_URL} ${CMAKE_BINARY_DIR}/${WASMTIME_ARCHIVE}
        SHOW_PROGRESS
        STATUS WASMTIME_DOWNLOAD_STATUS)

    list(GET WASMTIME_DOWNLOAD_STATUS 0 WASMTIME_ERROR)
    if(WASMTIME_ERROR)
        message(FATAL_ERROR "Failed to download Wasmtime from ${WASMTIME_URL}")
    endif()

    message(STATUS "Extracting Wasmtime to ${WASMTIME_DIR}")
    file(ARCHIVE_EXTRACT
        INPUT ${CMAKE_BINARY_DIR}/${WASMTIME_ARCHIVE}
        DESTINATION ${WASMTIME_DIR})
endif()

message(STATUS "Wasmtime library: ${WASMTIME_LIBRARY}")
message(STATUS "Wasmtime include dir: ${WASMTIME_INCLUDE_DIR}")

# Export variables for use in parent scope
set(WASMTIME_LIBRARY ${WASMTIME_LIBRARY} PARENT_SCOPE)
set(WASMTIME_INCLUDE_DIR ${WASMTIME_INCLUDE_DIR} PARENT_SCOPE)
set(WASMTIME_DLL ${WASMTIME_DLL} PARENT_SCOPE)
