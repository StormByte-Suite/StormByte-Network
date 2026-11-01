set(WITH_NGHTTP2 "BUNDLED" CACHE STRING "Use nghttp2 library (SYSTEM/BUNDLED)")

set_property(CACHE WITH_NGHTTP2 PROPERTY STRINGS SYSTEM BUNDLED)
if(NOT WITH_NGHTTP2 STREQUAL "SYSTEM" AND NOT WITH_NGHTTP2 STREQUAL "BUNDLED")
	message(FATAL_ERROR "WITH_NGHTTP2 must be SYSTEM or BUNDLED")
endif()

if(WITH_NGHTTP2 STREQUAL "SYSTEM")
	find_package(nghttp2 CONFIG QUIET)
	if(TARGET nghttp2::nghttp2)
		set(_nghttp2_target nghttp2::nghttp2)
	elseif(TARGET nghttp2::nghttp2_static)
		set(_nghttp2_target nghttp2::nghttp2_static)
	else()
		find_package(PkgConfig QUIET)
		if(PkgConfig_FOUND)
			pkg_check_modules(NGHTTP2 QUIET IMPORTED_TARGET libnghttp2)
		endif()

		if(TARGET PkgConfig::NGHTTP2)
			add_library(StormByteSystemNghttp2 INTERFACE IMPORTED GLOBAL)
			target_link_libraries(StormByteSystemNghttp2 INTERFACE PkgConfig::NGHTTP2)
		else()
			find_path(NGHTTP2_INCLUDE_DIR NAMES nghttp2/nghttp2.h)
			find_library(NGHTTP2_LIBRARY NAMES nghttp2 nghttp2_static)
			if(NOT NGHTTP2_INCLUDE_DIR OR NOT NGHTTP2_LIBRARY)
				message(FATAL_ERROR
					"WITH_NGHTTP2=SYSTEM requires nghttp2 headers and library; "
					"install nghttp2 or configure with -DWITH_NGHTTP2=BUNDLED")
			endif()
			add_library(StormByteSystemNghttp2 UNKNOWN IMPORTED GLOBAL)
			set_target_properties(StormByteSystemNghttp2 PROPERTIES
				IMPORTED_LOCATION "${NGHTTP2_LIBRARY}"
				INTERFACE_INCLUDE_DIRECTORIES "${NGHTTP2_INCLUDE_DIR}"
			)
		endif()
		set(_nghttp2_target StormByteSystemNghttp2)
	endif()

	get_target_property(_nghttp2_imported "${_nghttp2_target}" IMPORTED)
	if(_nghttp2_imported)
		set_target_properties("${_nghttp2_target}" PROPERTIES IMPORTED_GLOBAL TRUE)
	endif()
else()
	set(_nghttp2_version 1.70.0)
	if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/src/CMakeLists.txt")
		message(FATAL_ERROR
			"WITH_NGHTTP2=BUNDLED requires the nghttp2 source submodule. "
			"Run git submodule update --init --recursive.")
	endif()

	buildmaster_component(
		nghttp2
		"nghttp2 ${_nghttp2_version}"
		"${CMAKE_CURRENT_LIST_DIR}/cmake-port"
		"NGHTTP2_SOURCE=${CMAKE_CURRENT_LIST_DIR}/src"
		static
		nghttp2
		"ALIAS=StormByte::Nghttp2"
	)
	target_compile_definitions(nghttp2 INTERFACE NGHTTP2_STATICLIB)
	set(_nghttp2_target StormByte::Nghttp2)
endif()

buildmaster_link(StormByte-Network "${_nghttp2_target}")
