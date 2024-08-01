cmake_minimum_required(VERSION 3.7)
project(sync-max)
cmake_policy(SET CMP0054 NEW)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules)

#set(LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib")
set(MAYA_VERSION 2020 CACHE STRING "Maya version")

if(WIN32)
    set(MAYA_PLUGIN_OUTPUT_DIR "C:/Program Files/Autodesk/Maya${MAYA_VERSION}/bin/plug-ins/")
endif(WIN32)

# Max Sync
file(GLOB_RECURSE SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)
add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES})

# BGFX
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "Build bgfx examples.")
set(BGFX_BUILD_TOOLS    OFF CACHE BOOL "Build bgfx tools.")
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../bgfx bgfx)
target_link_libraries(${PROJECT_NAME} PRIVATE bx)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${BX_DIR}/include
)

# MAYA
find_package(Maya REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Maya::Maya)
target_include_directories(${PROJECT_NAME} PRIVATE 
    Maya::Maya
)
MAYA_PLUGIN(${PROJECT_NAME})

# Put in a "maxsync" folder in Visual Studio
set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "maxsync ")

# Copy mll to maya @todo
