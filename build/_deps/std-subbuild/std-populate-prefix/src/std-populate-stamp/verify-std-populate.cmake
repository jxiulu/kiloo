# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

if("/modules/c++/v1/" STREQUAL "")
  message(FATAL_ERROR "LOCAL can't be empty")
endif()

if(NOT EXISTS "/modules/c++/v1/")
  message(FATAL_ERROR "File not found: /modules/c++/v1/")
endif()

if("" STREQUAL "")
  message(WARNING "File cannot be verified since no URL_HASH specified")
  return()
endif()

if("" STREQUAL "")
  message(FATAL_ERROR "EXPECT_VALUE can't be empty")
endif()

message(VERBOSE "verifying file...
     file='/modules/c++/v1/'")

file("" "/modules/c++/v1/" actual_value)

if(NOT "${actual_value}" STREQUAL "")
  message(FATAL_ERROR "error:  hash of
  /modules/c++/v1/
does not match expected value
  expected: ''
    actual: '${actual_value}'
")
endif()

message(VERBOSE "verifying file... done")
