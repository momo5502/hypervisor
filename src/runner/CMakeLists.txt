file(GLOB_RECURSE runner_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
file(GLOB_RECURSE runner_headers CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)

add_executable(runner #WIN32
	${runner_sources}
	${runner_headers}
)

set_property(TARGET runner APPEND_STRING PROPERTY LINK_FLAGS " /MANIFESTUAC:\"level='requireAdministrator'\"")

target_link_libraries(runner PRIVATE
	library
)

set_source_files_properties(resource.rc PROPERTIES LANGUAGE RC)
target_sources(runner PRIVATE
	resource.rc
)
