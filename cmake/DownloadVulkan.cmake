# Download and setup Vulkan SDK for cross-compilation
# Only downloads if cross-compiling and Vulkan is not found on the system

# Vulkan SDK version to download
set(VULKAN_SDK_VERSION "1.3.280.0")
set(VULKAN_DIR ${CMAKE_BINARY_DIR}/vulkan-sdk)

# Only download if cross-compiling and Vulkan not already found
if(CMAKE_CROSSCOMPILING AND NOT Vulkan_FOUND)
    message(STATUS "Cross-compiling: Downloading Vulkan SDK for target platform...")

    # Determine platform and URLs based on TARGET_OS
    if(TARGET_OS STREQUAL "windows")
        # For Windows cross-compilation, we need the Windows SDK
        set(VULKAN_PLATFORM "windows")
        set(VULKAN_ARCHIVE "vulkan-sdk-${VULKAN_SDK_VERSION}-windows.zip")
        set(VULKAN_URL "https://sdk.lunarg.com/sdk/download/${VULKAN_SDK_VERSION}/windows/${VULKAN_ARCHIVE}")
        set(VULKAN_EXTRACTED_DIR "${VULKAN_DIR}/vulkan-sdk-${VULKAN_SDK_VERSION}")
        set(VULKAN_INCLUDE_DIR "${VULKAN_DIR}/Include")
        set(VULKAN_LIBRARY "${VULKAN_DIR}/Lib/vulkan-1.lib")

    elseif(TARGET_OS STREQUAL "linux")
        # For Linux cross-compilation
        set(VULKAN_PLATFORM "linux")
        set(VULKAN_ARCHIVE "vulkan-sdk-${VULKAN_SDK_VERSION}-linux.tar.gz")
        set(VULKAN_URL "https://sdk.lunarg.com/sdk/download/${VULKAN_SDK_VERSION}/linux/${VULKAN_ARCHIVE}")
        set(VULKAN_EXTRACTED_DIR "${VULKAN_DIR}/vulkan-sdk-${VULKAN_SDK_VERSION}")
        set(VULKAN_INCLUDE_DIR "${VULKAN_DIR}/x86_64/include")
        set(VULKAN_LIBRARY "${VULKAN_DIR}/x86_64/lib/libvulkan.so")

    elseif(TARGET_OS STREQUAL "macos")
        # For macOS cross-compilation
        set(VULKAN_PLATFORM "mac")
        set(VULKAN_ARCHIVE "vulkan-sdk-${VULKAN_SDK_VERSION}-mac.dmg")
        message(WARNING
            "macOS Vulkan SDK download not fully automated (requires DMG mounting).\n"
            "Please download manually from: https://vulkan.lunarg.com/\n"
            "Or install via: brew install molten-vk vulkan-loader vulkan-headers")
        set(DOWNLOAD_VULKAN FALSE)
    else()
        message(WARNING "Unknown TARGET_OS for Vulkan SDK download: ${TARGET_OS}")
        set(DOWNLOAD_VULKAN FALSE)
    endif()

    # Download and extract Vulkan SDK if library doesn't exist
    if(NOT DEFINED DOWNLOAD_VULKAN OR DOWNLOAD_VULKAN)
        if(NOT EXISTS ${VULKAN_LIBRARY})
            message(STATUS "Downloading Vulkan SDK ${VULKAN_SDK_VERSION} for ${VULKAN_PLATFORM}...")
            message(STATUS "URL: ${VULKAN_URL}")
            message(STATUS "This may take a while (SDK is ~200-500MB)...")

            file(DOWNLOAD ${VULKAN_URL} ${CMAKE_BINARY_DIR}/${VULKAN_ARCHIVE}
                SHOW_PROGRESS
                STATUS VULKAN_DOWNLOAD_STATUS
                TIMEOUT 600)

            list(GET VULKAN_DOWNLOAD_STATUS 0 VULKAN_ERROR)
            if(VULKAN_ERROR)
                message(WARNING
                    "Failed to download Vulkan SDK from ${VULKAN_URL}\n"
                    "You can:\n"
                    "  1. Download manually from https://vulkan.lunarg.com/\n"
                    "  2. Set Vulkan paths: -DVulkan_INCLUDE_DIR=... -DVulkan_LIBRARY=...\n"
                    "  3. Build without Vulkan (if optional in your code)")
                set(VULKAN_DOWNLOAD_FAILED TRUE)
            else()
                message(STATUS "Extracting Vulkan SDK to ${VULKAN_DIR}")
                file(ARCHIVE_EXTRACT
                    INPUT ${CMAKE_BINARY_DIR}/${VULKAN_ARCHIVE}
                    DESTINATION ${VULKAN_DIR})

                # Verify extraction
                if(NOT EXISTS ${VULKAN_LIBRARY})
                    message(WARNING
                        "Vulkan SDK extraction may have failed.\n"
                        "Expected library at: ${VULKAN_LIBRARY}")
                    set(VULKAN_DOWNLOAD_FAILED TRUE)
                endif()
            endif()
        endif()

        # Set Vulkan variables if download succeeded
        if(NOT VULKAN_DOWNLOAD_FAILED AND EXISTS ${VULKAN_LIBRARY})
            set(Vulkan_FOUND TRUE)
            set(Vulkan_FOUND TRUE PARENT_SCOPE)
            set(Vulkan_INCLUDE_DIR ${VULKAN_INCLUDE_DIR} PARENT_SCOPE)
            set(Vulkan_LIBRARY ${VULKAN_LIBRARY} PARENT_SCOPE)

            # Create Vulkan::Vulkan imported target if it doesn't exist
            # This mimics what find_package(Vulkan) does
            if(NOT TARGET Vulkan::Vulkan)
                add_library(Vulkan::Vulkan UNKNOWN IMPORTED)
                set_target_properties(Vulkan::Vulkan PROPERTIES
                    IMPORTED_LOCATION "${VULKAN_LIBRARY}"
                    INTERFACE_INCLUDE_DIRECTORIES "${VULKAN_INCLUDE_DIR}"
                )
            endif()

            message(STATUS "Vulkan SDK configured:")
            message(STATUS "  Include: ${VULKAN_INCLUDE_DIR}")
            message(STATUS "  Library: ${VULKAN_LIBRARY}")
        endif()
    endif()
elseif(NOT CMAKE_CROSSCOMPILING)
    # Native build - use system Vulkan
    message(STATUS "Native build: Using system Vulkan SDK")
endif()
