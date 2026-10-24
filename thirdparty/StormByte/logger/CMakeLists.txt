include("${CMAKE_SOURCE_DIR}/cmake/stormbyte.cmake")

set(STORMBYTE_LOGGER_VERSION "2.0.4")
stormbyte_component_url(STORMBYTE_LOGGER_URL "${STORMBYTE_LOGGER_VERSION}" "StormByte-Logger")

set(STORMBYTE_LOGGER_OPTSTR "BACKEND=binary;ALIAS=StormByte::Logger")

if(BUILD_SHARED_LIBS)
	set(STORMBYTE_LOGGER_MODE shared)
else()
	set(STORMBYTE_LOGGER_MODE static)
endif()

buildmaster_component(
	StormByte-Logger
	"StormByte Suite - Logger"
	"${STORMBYTE_LOGGER_URL}"
	""
	"${STORMBYTE_LOGGER_MODE}"
	"StormByte-Logger"
	"${STORMBYTE_LOGGER_OPTSTR}"
)
buildmaster_link(StormByte-Logger StormByte-Base)

if(WIN32 AND BUILD_SHARED_LIBS)
	target_compile_definitions(StormByte-Logger INTERFACE STORMBYTE_LOGGER_SHARED)
endif()
