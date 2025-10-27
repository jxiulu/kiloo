cmake_minimum_required(VERSION 3.30 FATAL_ERROR)
project(autotools LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)

add_library(terminal INTERFACE core/terminal.hpp)

add_library(core INTERFACE core/editor.hpp core/tui.hpp core/extensions.cpp
            core/extensions.hpp)
target_include_directories(core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/core)

add_library(ai_ext INTERFACE ext/ai.hpp)
target_include_directories(ai_ext INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/ext
                                            ${CMAKE_CURRENT_SOURCE_DIR}/core)

add_executable(main core/main.cpp)
target_link_libraries(main PRIVATE core ai_ext nlohmann_json::nlohmann_json)
