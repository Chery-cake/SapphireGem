# Generate .clangd configuration file for IDE support

set(projects
    steamworks
    vulkan
    vma
    btp
    stb
    wasmtime
)

set(inc_dirs)
foreach(proj ${projects})
    if(TARGET ${proj})
        get_target_property(dirs ${proj} INTERFACE_INCLUDE_DIRECTORIES)
        if(dirs)
            list(APPEND inc_dirs ${dirs})
        endif()
    endif()
endforeach()

set(clangd_includes)
foreach(dir ${inc_dirs})
    if(IS_ABSOLUTE "${dir}" AND EXISTS "${dir}")
        file(TO_NATIVE_PATH "${dir}" native_dir)
        string(REPLACE "\\" "\\\\" native_dir "${native_dir}")
        list(APPEND clangd_includes "- \"-I${native_dir}\"\n")
    endif()
endforeach()

if(clangd_includes)
    list(REMOVE_DUPLICATES clangd_includes)
endif()
string(REPLACE ";" "        " clangd_includes "${clangd_includes}")

set(clangd_content "
CompileFlags:
    Add:
        ${clangd_includes}
        - \"-std=c${CMAKE_C_STANDARD}\"
        - \"-std=c++${CMAKE_CXX_STANDARD}\"
        - \"-std=gnu++${CMAKE_CXX_STANDARD}\"
        - \"-Wall\"
        - \"-Wextra\"
        - \"-Wpedantic\"
    CompilationDatabase: ${CMAKE_BINARY_DIR}

Diagnostics:
    ClangTidy:
        Add: ['bugprone-*', 'performance-*', 'readability-*', 'portability-*', 'concurrency-*', 'cppcoreguidelines-*']
    UnusedIncludes: Strict
    MissingIncludes: Strict

Index:
    Background: Build
")

file(WRITE "${CMAKE_SOURCE_DIR}/.clangd" "${clangd_content}")
message(STATUS "Generated .clangd configuration file")
