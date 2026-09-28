# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 HarriethWiKk
cmake_minimum_required(VERSION 3.22)

project(image-client
        VERSION 0.1.0
        DESCRIPTION "Local image generation client (C++/QML)"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/utf-8 /W4 /permissive-)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network Sql Test)

qt_standard_project_setup(REQUIRES 6.8)

add_subdirectory(src/core)
add_subdirectory(src/app)
add_subdirectory(src/gui)
add_subdirectory(src/mcp)

enable_testing()
add_subdirectory(tests)
