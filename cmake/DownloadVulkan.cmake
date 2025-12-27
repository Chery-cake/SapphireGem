# Download and setup Vulkan SDK for cross-compilation
# Only downloads if cross-compiling and Vulkan is not found on the system

# Vulkan SDK version to download
set(VULKAN_SDK_VERSION "1.3.280.0")
set(VULKAN_DIR ${CMAKE_BINARY_DIR}/vulkan-sdk)

# Only download if cross-compiling and Vulkan not already found
if(CMAKE_CROSSCOMPILING AND NOT Vulkan_FOUND)
    message(STATUS "Cross-compiling: Attempting to find Vulkan SDK for target platform...")
    
    # Determine platform and URLs based on TARGET_OS
    if(TARGET_OS STREQUAL "windows")
        # For Windows cross-compilation
        # Windows Vulkan SDK comes as an installer which can't be used for cross-compilation
        # Users need to manually extract or we provide pre-extracted minimal runtime
        message(STATUS "Windows cross-compilation detected")
        message(STATUS "Checking for manually provided Vulkan SDK...")
        
        # Check common manual installation paths
        set(VULKAN_MANUAL_PATHS
            "${VULKAN_DIR}/Include"
            "$ENV{VULKAN_SDK}/Include"
            "/usr/x86_64-w64-mingw32/include/vulkan"
        )
        
        foreach(MANUAL_PATH ${VULKAN_MANUAL_PATHS})
            if(EXISTS "${MANUAL_PATH}/vulkan/vulkan.h")
                get_filename_component(VULKAN_INCLUDE_DIR "${MANUAL_PATH}" ABSOLUTE)
                message(STATUS "Found Vulkan headers at: ${VULKAN_INCLUDE_DIR}")
                break()
            endif()
        endforeach()
        
        # Check for library
        set(VULKAN_LIB_PATHS
            "${VULKAN_DIR}/Lib"
            "$ENV{VULKAN_SDK}/Lib"
            "/usr/x86_64-w64-mingw32/lib"
        )
        
        foreach(LIB_PATH ${VULKAN_LIB_PATHS})
            if(EXISTS "${LIB_PATH}/vulkan-1.lib" OR EXISTS "${LIB_PATH}/libvulkan-1.a")
                if(EXISTS "${LIB_PATH}/vulkan-1.lib")
                    set(VULKAN_LIBRARY "${LIB_PATH}/vulkan-1.lib")
                else()
                    set(VULKAN_LIBRARY "${LIB_PATH}/libvulkan-1.a")
                endif()
                message(STATUS "Found Vulkan library at: ${VULKAN_LIBRARY}")
                break()
            endif()
        endforeach()
        
        # If not found, provide helpful message
        if(NOT DEFINED VULKAN_INCLUDE_DIR OR NOT DEFINED VULKAN_LIBRARY)
            message(WARNING 
                "Vulkan SDK for Windows not found for cross-compilation.\n"
                "\n"
                "To enable Vulkan for Windows cross-compilation:\n"
                "1. Download Windows Vulkan SDK from: https://vulkan.lunarg.com/\n"
                "2. Extract the installer contents or copy from a Windows installation:\n"
                "   - Headers: Include/vulkan/*.h\n"
                "   - Library: Lib/vulkan-1.lib\n"
                "3. Set environment variable: export VULKAN_SDK=/path/to/extracted/sdk\n"
                "   Or specify: -DVulkan_INCLUDE_DIR=/path/include -DVulkan_LIBRARY=/path/lib/vulkan-1.lib\n"
                "\n"
                "Alternatively, install MinGW Vulkan development package if available.")
            set(VULKAN_DOWNLOAD_FAILED TRUE)
        endif()
        
    elseif(TARGET_OS STREQUAL "linux")
        # For Linux cross-compilation - can actually download
        set(VULKAN_PLATFORM "linux")
        set(VULKAN_ARCHIVE "vulkansdk-linux-x86_64-${VULKAN_SDK_VERSION}.tar.gz")
        set(VULKAN_URL "https://sdk.lunarg.com/sdk/download/${VULKAN_SDK_VERSION}/linux/${VULKAN_ARCHIVE}")
        set(VULKAN_EXTRACTED_DIR "${VULKAN_DIR}/${VULKAN_SDK_VERSION}/x86_64")
        set(VULKAN_INCLUDE_DIR "${VULKAN_EXTRACTED_DIR}/include")
        set(VULKAN_LIBRARY "${VULKAN_EXTRACTED_DIR}/lib/libvulkan.so.1")
        set(CAN_AUTO_DOWNLOAD TRUE)
        
    elseif(TARGET_OS STREQUAL "macos")
        # For macOS cross-compilation
        set(VULKAN_PLATFORM "mac")
        message(WARNING 
            "macOS Vulkan SDK cross-compilation requires manual setup.\n"
            "Please download from: https://vulkan.lunarg.com/\n"
            "Or install via: brew install molten-vk vulkan-loader vulkan-headers")
        set(VULKAN_DOWNLOAD_FAILED TRUE)
    else()
        message(WARNING "Unknown TARGET_OS for Vulkan SDK: ${TARGET_OS}")
        set(VULKAN_DOWNLOAD_FAILED TRUE)
    endif()
    
    # Download and extract Vulkan SDK for Linux (only platform with direct download)
    if(DEFINED CAN_AUTO_DOWNLOAD AND CAN_AUTO_DOWNLOAD)
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
                list(GET VULKAN_DOWNLOAD_STATUS 1 VULKAN_ERROR_MSG)
                message(WARNING 
                    "Failed to download Vulkan SDK: ${VULKAN_ERROR_MSG}\n"
                    "URL: ${VULKAN_URL}\n"
                    "\n"
                    "You can:\n"
                    "  1. Download manually from https://vulkan.lunarg.com/\n"
                    "  2. Set Vulkan paths: -DVulkan_INCLUDE_DIR=... -DVulkan_LIBRARY=...\n"
                    "  3. Build natively for your current OS")
                set(VULKAN_DOWNLOAD_FAILED TRUE)
            else()
                message(STATUS "Extracting Vulkan SDK to ${VULKAN_DIR}")
                file(ARCHIVE_EXTRACT
                    INPUT ${CMAKE_BINARY_DIR}/${VULKAN_ARCHIVE}
                    DESTINATION ${VULKAN_DIR})
                
                # Verify extraction
                if(NOT EXISTS ${VULKAN_LIBRARY})
                    # List what was actually extracted to help debug
                    file(GLOB_RECURSE EXTRACTED_FILES "${VULKAN_DIR}/*vulkan*")
                    message(WARNING 
                        "Vulkan SDK extraction may have failed.\n"
                        "Expected library at: ${VULKAN_LIBRARY}\n"
                        "Extracted directory: ${VULKAN_DIR}\n"
                        "Found files: ${EXTRACTED_FILES}")
                    set(VULKAN_DOWNLOAD_FAILED TRUE)
                endif()
            endif()
        endif()
    endif()
    
    # Set Vulkan variables if found
    if(NOT VULKAN_DOWNLOAD_FAILED AND DEFINED VULKAN_INCLUDE_DIR AND DEFINED VULKAN_LIBRARY)
        if(EXISTS ${VULKAN_LIBRARY})
            set(Vulkan_FOUND TRUE PARENT_SCOPE)
            set(Vulkan_INCLUDE_DIR ${VULKAN_INCLUDE_DIR} PARENT_SCOPE)
            set(Vulkan_LIBRARY ${VULKAN_LIBRARY} PARENT_SCOPE)
            
            message(STATUS "Vulkan SDK configured:")
            message(STATUS "  Include: ${VULKAN_INCLUDE_DIR}")
            message(STATUS "  Library: ${VULKAN_LIBRARY}")
        endif()
    endif()
elseif(NOT CMAKE_CROSSCOMPILING)
    # Native build - use system Vulkan
    message(STATUS "Native build: Using system Vulkan SDK")
endif()
