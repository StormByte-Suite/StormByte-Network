if(WIN32)
	set(WITH_STORMBYTE "BUNDLED" CACHE STRING "Use StormByte components (SYSTEM/BUNDLED)" FORCE)
else()
	set(WITH_STORMBYTE "BUNDLED" CACHE STRING "Use StormByte components (SYSTEM/BUNDLED)")
endif()

if(WITH_STORMBYTE STREQUAL "SYSTEM")
	find_package(StormByte REQUIRED Base Buffer Logger System)
else()
	add_subdirectory(base)
	add_subdirectory(logger)
	add_subdirectory(system)
	add_subdirectory(buffer)
	buildmaster_link(StormByte-Logger StormByte-Base)
	buildmaster_link(StormByte-System StormByte-Base)
	buildmaster_link(StormByte-Buffer StormByte-Base StormByte-Logger StormByte-System)
endif()

buildmaster_link(StormByte-Network StormByte-Base StormByte-Buffer StormByte-Logger StormByte-System)

# doc/ is added before this directory, so the unpack targets do not exist yet there.
if(TARGET StormByteNetworkDoc)
	foreach(_doc_dep IN ITEMS
			_bm_bin_StormByte-Base_unpack
			_bm_bin_StormByte-Logger_unpack
			_bm_bin_StormByte-System_unpack
			_bm_bin_StormByte-Buffer_unpack)
		if(TARGET "${_doc_dep}")
			add_dependencies(StormByteNetworkDoc "${_doc_dep}")
		endif()
	endforeach()
endif()
