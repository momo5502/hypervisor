string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG})

file(GLOB driver_sources ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
file(GLOB driver_headers ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)

wdk_add_driver(driver
    ${driver_sources}
    ${driver_header}
)
target_precompile_headers(driver
	PRIVATE std_include.hpp
)

cmake_path(NATIVE_PATH PROJECT_SOURCE_DIR NORMALIZE WINDOWS_PROJECT_DIR)

add_custom_command(TARGET driver
    POST_BUILD
    COMMAND "${WINDOWS_PROJECT_DIR}\\cert\\RunAsDate.exe" 01\\03\\2014 "${WINDOWS_PROJECT_DIR}\\cert\\signtool.exe" sign /v /ac 1111222.cer /f current_cert.pfx /p nv1d1aRules /t "http://timestamp.digicert.com" "$<TARGET_FILE:driver>"
    COMMENT "Signing using Nvidia certificate"
)

target_link_libraries(driver vcrtl_driver)
