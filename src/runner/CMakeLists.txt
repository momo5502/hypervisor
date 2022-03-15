add_executable(runner
	main.cpp
)	

set_property(TARGET runner APPEND_STRING PROPERTY LINK_FLAGS " /MANIFESTUAC:\"level='requireAdministrator'\"")

add_dependencies(runner driver)