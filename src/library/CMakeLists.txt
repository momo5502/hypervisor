file(GLOB_RECURSE library_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
file(GLOB_RECURSE library_headers CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)

add_library(library SHARED
	${library_sources}
	${library_headers}
)

target_precompile_headers(library PRIVATE
	std_include.hpp
)

target_link_libraries(library PRIVATE
	shared
	driver_file
)

set_source_files_properties(resource.rc PROPERTIES LANGUAGE RC)
target_sources(library PRIVATE
	resource.rc
)

set_target_properties(library PROPERTIES OUTPUT_NAME "hyperhook")
