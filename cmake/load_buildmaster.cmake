if(NOT COMMAND buildmaster_component)
	find_package(StormByte-BuildMaster 2.1.0 CONFIG QUIET)

	if(NOT StormByte-BuildMaster_FOUND)
		add_subdirectory("${CMAKE_SOURCE_DIR}/buildmaster"
			"${CMAKE_BINARY_DIR}/buildmaster")
	endif()
endif()
