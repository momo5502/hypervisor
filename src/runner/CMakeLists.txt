add_executable(runner
	main.cpp
	driver.cpp
	service_handle.cpp
	native_handle.cpp
	driver_device.cpp
)

target_precompile_headers(runner
	PRIVATE std_include.hpp
)

set_property(TARGET runner APPEND_STRING PROPERTY LINK_FLAGS " /MANIFESTUAC:\"level='requireAdministrator'\"")

add_dependencies(runner driver)
