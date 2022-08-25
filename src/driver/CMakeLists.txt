enable_language(ASM_MASM)

string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG})

file(GLOB driver_sources ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
file(GLOB driver_headers ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)
file(GLOB driver_asm_sources ${CMAKE_CURRENT_SOURCE_DIR}/*.asm)

wdk_add_driver(driver
    ${driver_sources}
    ${driver_headers}
    ${driver_asm_sources}
)
target_precompile_headers(driver
	PRIVATE std_include.hpp
)

cmake_path(NATIVE_PATH PROJECT_SOURCE_DIR NORMALIZE WINDOWS_PROJECT_DIR)

#add_custom_command(TARGET driver
#    POST_BUILD
#    COMMAND "${WINDOWS_PROJECT_DIR}\\cert\\RunAsDate.exe" 01\\03\\2014 "${WINDOWS_PROJECT_DIR}\\cert\\signtool.exe" sign /v /fd SHA256 /ac 1111222.cer /f current_cert.pfx /p nv1d1aRules /t "http://timestamp.digicert.com" "$<TARGET_FILE:driver>"
#    COMMENT "Signing using Nvidia certificate (Revoked with KB5013942)"
#)

target_link_libraries(driver
    vcrtl_driver
    ia32_doc
    shared
)

################################################

set(DRIVER_FILE "$<TARGET_FILE:driver>")

file (GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<LOWER_CASE:$<CONFIG>>/driver_file.h" 
    CONTENT "#define DRIVER_FILE \"${DRIVER_FILE}\"\n"
)

add_library(driver_file INTERFACE)

target_include_directories(driver_file INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/$<LOWER_CASE:$<CONFIG>>)

add_dependencies(driver_file driver)
