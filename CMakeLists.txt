cmake_minimum_required(VERSION 3.7)
project(maya-bridge)
cmake_policy(SET CMP0054 NEW)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules)

# Makefile generators on apple need this flag to compile mixed objective/c++
if(APPLE AND NOT XCODE)
	set(CMAKE_CXX_FLAGS "-ObjC++")
endif()

if(MSVC AND (MSVC_VERSION GREATER_EQUAL 1914))
	add_compile_options("/Zc:__cplusplus")
endif()

include(CMakeDependentOption)

option(BX_AMALGAMATED "Amalgamated bx build for faster compilation" OFF)
option(BX_CONFIG_DEBUG "Log debug messages (default: on in debug)" OFF)

if(NOT BX_DIR)
	set(BX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bx" CACHE STRING "Location of bx.")
elseif(NOT IS_ABSOLUTE "${BX_DIR}")
	get_filename_component(BX_DIR "${BX_DIR}" REALPATH BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# Plugin
file(GLOB_RECURSE SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)
add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES})

# BX
file(TO_CMAKE_PATH "${BX_DIR}" BX_DIR)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/cmake/bx)

# MAYA
set(MAYA_VERSION 2020 CACHE STRING "Maya version")
find_package(Maya REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Maya::Maya bx)
target_include_directories(${PROJECT_NAME} PRIVATE 
    Maya::Maya
    ${BX_DIR}/include
)
MAYA_PLUGIN(${PROJECT_NAME})

# Put in a "maya-bridge" folder in Visual Studio
set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "maya-bridge ")
