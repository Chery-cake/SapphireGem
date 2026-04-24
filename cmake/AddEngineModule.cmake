# cmake/AddEngineModule.cmake
include(GenerateExportHeader)

function(add_engine_module TARGET_NAME)
    # Assume the target already exists (add_library called before this)
    # Generate export header with standard naming:
    #   BASE_NAME = uppercase target name
    #   EXPORT_MACRO_NAME = <BASE_NAME>_API
    #   EXPORT_FILE_NAME = <target_name>_export.h
    string(TOUPPER ${TARGET_NAME} BASE_NAME_UPPER)
    set(EXPORT_MACRO_NAME "${BASE_NAME_UPPER}_API")
    set(EXPORT_HEADER_NAME "${TARGET_NAME}_export.h")

    generate_export_header(${TARGET_NAME}
        BASE_NAME ${BASE_NAME_UPPER}
        EXPORT_MACRO_NAME ${EXPORT_MACRO_NAME}
        EXPORT_FILE_NAME ${EXPORT_HEADER_NAME}
    )

    # Ensure every module sees the <MODULE>_EXPORTS definition
    # that its manual export header expects (e.g. WINDOW_EXPORTS).
    target_compile_definitions(${TARGET_NAME} PRIVATE ${BASE_NAME_UPPER}_EXPORTS)

    # Ensure the binary directory is in the include path for this target
    # Add binary dir ONLY for build interface
    target_include_directories(${TARGET_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    )

    # Install the generated header along with public headers
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${EXPORT_HEADER_NAME}
        DESTINATION include/${PROJECT_NAME}
    )
endfunction()
