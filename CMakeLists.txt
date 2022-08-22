cmake_minimum_required(VERSION 3.21)

project(DeviceSimulator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Network)

include(CTest)

if(BUILD_TESTING)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test)
endif()

add_subdirectory(src/core)
add_subdirectory(src/cli)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
