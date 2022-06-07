file(GLOB_RECURSE runner_sources ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
file(GLOB_RECURSE  runner_headers ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)

add_library(hyperhook SHARED #WIN32
	${runner_sources}
	${runner_headers}
)

target_precompile_headers(hyperhook
	PRIVATE std_include.hpp
)

set_property(TARGET hyperhook APPEND_STRING PROPERTY LINK_FLAGS " /MANIFESTUAC:\"level='requireAdministrator'\"")

target_link_libraries(hyperhook
	shared
	driver_file
)

set_source_files_properties(resource.rc PROPERTIES LANGUAGE RC)
target_sources(hyperhook PRIVATE
	resource.rc
)
