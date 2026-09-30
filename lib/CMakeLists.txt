include(GNUInstallDirs)

file(GLOB_RECURSE STORMBYTE_NETWORK_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.cxx")

if(BUILD_SHARED_LIBS)
	set(_mode shared)
else()
	set(_mode static)
endif()

buildmaster_component(
	StormByte-Network
	"StormByte Suite - Network"
	"${STORMBYTE_NETWORK_SOURCES}"
	""
	${_mode}
	"StormByte-Network"
	"BACKEND=host;ALIAS=StormByte::Network"
)

if(BUILD_SHARED_LIBS)
	target_compile_definitions(StormByte-Network
		PRIVATE StormByte_Network_EXPORTS
		PUBLIC STORMBYTE_NETWORK_SHARED
	)
endif()

set_target_properties(StormByte-Network PROPERTIES
	LINKER_LANGUAGE CXX
	SOVERSION ${CMAKE_PROJECT_VERSION}
	VERSION ${CMAKE_PROJECT_VERSION}
	POSITION_INDEPENDENT_CODE ON
	CXX_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN ON
)

if(WIN32)
	target_link_libraries(StormByte-Network PRIVATE ws2_32 iphlpapi)
endif()

if(MSVC)
	target_compile_options(StormByte-Network PRIVATE
		/EHsc
		$<$<CONFIG:Release>:/O2>
		$<$<CONFIG:Debug>:/Od>
	)
	target_compile_definitions(StormByte-Network PUBLIC UNICODE NOMINMAX)
else()
	target_compile_options(StormByte-Network PRIVATE
		$<$<CONFIG:Debug>:-pipe -g -ggdb -Wall -Wextra -Wnon-virtual-dtor -pedantic -pedantic-errors -O0>
	)
endif()

target_include_directories(StormByte-Network
	SYSTEM BEFORE PUBLIC
		"${CMAKE_CURRENT_LIST_DIR}/public"
		"${CMAKE_CURRENT_LIST_DIR}/private"
)
