##########################################

macro(set_artifact_directory directory)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${directory})
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${directory})
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${directory})
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${directory})
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${directory})
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${directory})
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${directory})
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${directory})
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${directory})
endmacro()

##########################################

macro(set_new_artifact_directory)
  get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
  if(IS_MULTI_CONFIG)
      set(ARTIFACT_FOLDER_NAME "artifacts-$<LOWER_CASE:$<CONFIG>>")
  else()
      set(ARTIFACT_FOLDER_NAME "artifacts")
  endif()

  set(ARTIFACT_DIRECTORY "${CMAKE_BINARY_DIR}/${ARTIFACT_FOLDER_NAME}")
  set_artifact_directory(${ARTIFACT_DIRECTORY})
endmacro()

##########################################

macro(enable_driver_support)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/external/FindWDK/cmake")
  find_package(WDK REQUIRED)
endmacro()