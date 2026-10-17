include("${CMAKE_SOURCE_DIR}/cmake/stormbyte.cmake")

set(STORMBYTE_SYSTEM_VERSION "2.0.2")
stormbyte_component_url(STORMBYTE_SYSTEM_URL "${STORMBYTE_SYSTEM_VERSION}" "StormByte-System")

set(STORMBYTE_SYSTEM_OPTSTR "BACKEND=binary;ALIAS=StormByte::System")

if(BUILD_SHARED_LIBS)
	set(STORMBYTE_SYSTEM_MODE shared)
else()
	set(STORMBYTE_SYSTEM_MODE static)
endif()

buildmaster_component(
	StormByte-System
	"StormByte Suite - System"
	"${STORMBYTE_SYSTEM_URL}"
	""
	"${STORMBYTE_SYSTEM_MODE}"
	"StormByte-System"
	"${STORMBYTE_SYSTEM_OPTSTR}"
)

if(WIN32 AND BUILD_SHARED_LIBS)
	target_compile_definitions(StormByte-System INTERFACE STORMBYTE_SYSTEM_SHARED)
endif()
