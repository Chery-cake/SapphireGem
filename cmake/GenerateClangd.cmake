# GenerateClangd.cmake
# Generates a .clangd configuration file for the project

function(generate_clangd_file)
    set(CLANGD_CONTENT "CompileFlags:
  CompilationDatabase: ${CMAKE_BINARY_DIR}
  Add:
    - -std=c17
    - -std=c++23
    - -Wall
    - -Wextra
    - -Wpedantic
  Remove:
    - -W*
    
Diagnostics:
  ClangTidy:
    Add:
      - modernize-*
      - performance-*
      - readability-*
    Remove:
      - modernize-use-trailing-return-type
      - readability-identifier-length
      
Index:
  Background: Build

InlayHints:
  Enabled: true
  ParameterNames: true
  DeducedTypes: true
")
    
    file(WRITE ${CMAKE_SOURCE_DIR}/.clangd "${CLANGD_CONTENT}")
    message(STATUS "Generated .clangd configuration file")
endfunction()
