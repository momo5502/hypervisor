include_guard()

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
  list(REMOVE_ITEM WDK_COMPILE_FLAGS /kernel)
endmacro()

##########################################

function(target_set_warnings_as_errors target)
  get_target_property(target_type ${target} TYPE)
  if(("${target_type}" STREQUAL "INTERFACE_LIBRARY") OR ("${target_type}" STREQUAL "UTILITY"))
    return()
  endif()

  set(compile_options)

  if(MSVC)
    set(compile_options /W4 /WX)
    if (CLANG)
      set(compile_options ${compile_options} -Xclang -Wconversion)
    endif()
  else()
    # lots of warnings and all warnings as errors
    set(compile_options -Wall -Wextra -Wconversion -pedantic -Werror)
  endif()

  target_compile_options(${target} PRIVATE
    $<$<COMPILE_LANGUAGE:C>:$<$<CONFIG:RELEASE>:${compile_options}>>
    $<$<COMPILE_LANGUAGE:CXX>:$<$<CONFIG:RELEASE>:${compile_options}>>
  )
endfunction()

##########################################

function(targets_set_warnings_as_errors)
  foreach(target ${ARGV})
    target_set_warnings_as_errors(${target})
  endforeach()
endfunction()

##########################################

function(get_all_targets var)
  set(targets)
  get_all_targets_recursive(targets ${CMAKE_CURRENT_SOURCE_DIR})
  set(${var} ${targets} PARENT_SCOPE)
endfunction()

##########################################

macro(get_all_targets_recursive targets dir)
  get_property(subdirectories DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
  foreach(subdir ${subdirectories})
      get_all_targets_recursive(${targets} ${subdir})
  endforeach()

  get_property(current_targets DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)
  list(APPEND ${targets} ${current_targets})
endmacro()

##########################################

macro(list_difference list_a list_to_remove result)
  set(${result} ${list_a})
  list(REMOVE_ITEM ${result} ${list_to_remove})
endmacro()

##########################################

macro(add_subdirectory_and_get_targets directory targets)
  get_all_targets(EXISTING_TARGETS)
  add_subdirectory(${directory})
  get_all_targets(ALL_TARGETS)

  list_difference("${ALL_TARGETS}" "${EXISTING_TARGETS}" ${targets})
endmacro()
