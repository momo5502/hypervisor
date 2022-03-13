cmake_minimum_required(VERSION 3.10)

project(secret)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/external/FindWDK/cmake")
find_package(WDK REQUIRED)

add_subdirectory(src)