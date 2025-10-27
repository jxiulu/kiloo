# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/jerrylu/Documents/Projects/autotools/build/_deps/std-src")
  file(MAKE_DIRECTORY "/Users/jerrylu/Documents/Projects/autotools/build/_deps/std-src")
endif()
file(MAKE_DIRECTORY
  "/Users/jerrylu/Documents/Projects/autotools/build/_deps/std-build"
  "/Users/jerrylu/Documents/Projects/autotools/build/_deps/std-subbuild/std-populate-prefix"
  "/Users/jerrylu/Documents/Projects/autotools/build/_deps/std-subbuild/std-populate-prefix/tmp"
  "/Users/jerrylu/Documents/Projects/autotools/build/_deps/std-subbuild/std-populate-prefix/src/std-populate-stamp"
  "/Users/jerrylu/Documents/Projects/autotools/build/_deps/std-subbuild/std-populate-prefix/src"
  "/Users/jerrylu/Documents/Projects/autotools/build/_deps/std-subbuild/std-populate-prefix/src/std-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/jerrylu/Documents/Projects/autotools/build/_deps/std-subbuild/std-populate-prefix/src/std-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/jerrylu/Documents/Projects/autotools/build/_deps/std-subbuild/std-populate-prefix/src/std-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
