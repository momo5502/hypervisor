file(GLOB runner_sources ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
file(GLOB runner_headers ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)

add_executable(runner
	${runner_sources}
	${runner_headers}
)

target_precompile_headers(runner
	PRIVATE std_include.hpp
)

set_property(TARGET runner APPEND_STRING PROPERTY LINK_FLAGS " /MANIFESTUAC:\"level='requireAdministrator'\"")

add_dependencies(runner driver)
