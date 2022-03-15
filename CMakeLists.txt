cmake_minimum_required(VERSION 3.20)

set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

project(secret LANGUAGES C CXX)

##########################################

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

##########################################

include(cmake/utils.cmake)

##########################################

set_new_artifact_directory()
enable_driver_support()

##########################################

add_subdirectory(src)
