include("${CMAKE_SOURCE_DIR}/cmake/stormbyte.cmake")

set(STORMBYTE_BASE_VERSION "2.2.2")
stormbyte_component_url(STORMBYTE_BASE_URL "${STORMBYTE_BASE_VERSION}")

set(STORMBYTE_BASE_OPTSTR "BACKEND=binary;ALIAS=StormByte::Base")

if(BUILD_SHARED_LIBS)
	set(STORMBYTE_BASE_MODE shared)
else()
	set(STORMBYTE_BASE_MODE static)
endif()

buildmaster_component(
	StormByte-Base
	"StormByte Suite - Base"
	"${STORMBYTE_BASE_URL}"
	""
	"${STORMBYTE_BASE_MODE}"
	"StormByte"
	"${STORMBYTE_BASE_OPTSTR}"
)

if(WIN32 AND BUILD_SHARED_LIBS)
	target_compile_definitions(StormByte-Base INTERFACE STORMBYTE_SHARED)
endif()
