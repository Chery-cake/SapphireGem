# GenerateClangd.cmake
# Generates a .clangd configuration file for the project


set(CLANGD_CONTENT "# File generated automatically
CompileFlags:
  CompilationDatabase: ${CMAKE_BINARY_DIR}
  Add:
    - -std=c23
    - -std=c++23
    - -Wall
    - -Wextra
    - -Wpedantic
  Remove:
    - -W*

HeaderInsertion: iwyu

Diagnostics:
  UnusedIncludes: true
  Spelling: true
  ClangTidy:
    Add:
      - modernize-*
      - performance-*
      - readability-*
      - concurrency-*
      - portability-*
      - bugprone-*
    Remove:
      - modernize-use-trailing-return-type
      - readability-identifier-length
      
Index:
  Background: Build

InlayHints:
  Enabled: true
  ParameterNames: true
  DeducedTypes: true

Hover:
  ShowAKA: Yes

Completion:
  AllScopes: true

IncludeCleaner:
  RemoveHeaders: true
  IgnoreStdHeaders: false
")
    
file(WRITE ${CMAKE_SOURCE_DIR}/.clangd "${CLANGD_CONTENT}")
message(STATUS "Generated .clangd configuration file")
