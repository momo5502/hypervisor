set(DRIVER_FILE "$<TARGET_FILE:driver>")

file (GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<LOWER_CASE:$<CONFIG>>/driver_file.h" 
    CONTENT "#define DRIVER_FILE \"${DRIVER_FILE}\"\n"
)

add_library(driver_file INTERFACE)

target_include_directories(driver_file INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/$<LOWER_CASE:$<CONFIG>>)

add_dependencies(driver_file driver)
