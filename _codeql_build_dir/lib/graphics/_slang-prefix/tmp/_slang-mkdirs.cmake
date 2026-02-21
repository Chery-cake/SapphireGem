# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/runner/work/SapphireGem/SapphireGem/_codeql_build_dir/lib/graphics/_slang-prefix/src/_slang")
  file(MAKE_DIRECTORY "/home/runner/work/SapphireGem/SapphireGem/_codeql_build_dir/lib/graphics/_slang-prefix/src/_slang")
endif()
file(MAKE_DIRECTORY
  "/home/runner/work/SapphireGem/SapphireGem/_codeql_build_dir/lib/graphics/_slang-prefix/src/_slang-build"
  "/home/runner/work/SapphireGem/SapphireGem/_codeql_build_dir/lib/graphics/_slang-prefix"
  "/home/runner/work/SapphireGem/SapphireGem/_codeql_build_dir/lib/graphics/_slang-prefix/tmp"
  "/home/runner/work/SapphireGem/SapphireGem/_codeql_build_dir/lib/graphics/_slang-prefix/src/_slang-stamp"
  "/home/runner/work/SapphireGem/SapphireGem/_codeql_build_dir/lib/graphics/_slang-prefix/src"
  "/home/runner/work/SapphireGem/SapphireGem/_codeql_build_dir/lib/graphics/_slang-prefix/src/_slang-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/runner/work/SapphireGem/SapphireGem/_codeql_build_dir/lib/graphics/_slang-prefix/src/_slang-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/runner/work/SapphireGem/SapphireGem/_codeql_build_dir/lib/graphics/_slang-prefix/src/_slang-stamp${cfgdir}") # cfgdir has leading slash
endif()
