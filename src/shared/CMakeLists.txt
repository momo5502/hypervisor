file(GLOB shared_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
file(GLOB shared_headers CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)

add_library(shared INTERFACE
	${shared_headers}
)

target_include_directories(shared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
