include("${CMAKE_SOURCE_DIR}/cmake/stormbyte.cmake")

set(STORMBYTE_BUFFER_VERSION "2.0.3")
stormbyte_component_url(STORMBYTE_BUFFER_URL "${STORMBYTE_BUFFER_VERSION}" "StormByte-Buffer")

set(STORMBYTE_BUFFER_OPTSTR "BACKEND=binary;ALIAS=StormByte::Buffer")

if(BUILD_SHARED_LIBS)
	set(STORMBYTE_BUFFER_MODE shared)
else()
	set(STORMBYTE_BUFFER_MODE static)
endif()

buildmaster_component(
	StormByte-Buffer
	"StormByte Suite - Buffer"
	"${STORMBYTE_BUFFER_URL}"
	""
	"${STORMBYTE_BUFFER_MODE}"
	"StormByte-Buffer"
	"${STORMBYTE_BUFFER_OPTSTR}"
)
buildmaster_link(StormByte-Buffer StormByte-Base StormByte-Logger StormByte-System)

if(WIN32 AND BUILD_SHARED_LIBS)
	target_compile_definitions(StormByte-Buffer INTERFACE STORMBYTE_BUFFER_SHARED)
endif()
