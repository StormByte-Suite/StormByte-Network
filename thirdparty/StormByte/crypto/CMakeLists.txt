include("${CMAKE_SOURCE_DIR}/cmake/stormbyte.cmake")

set(STORMBYTE_CRYPTO_VERSION "2.3.1")
stormbyte_component_url(STORMBYTE_CRYPTO_URL "${STORMBYTE_CRYPTO_VERSION}" "StormByte-Crypto")

set(STORMBYTE_CRYPTO_OPTSTR "BACKEND=binary;ALIAS=StormByte::Crypto")

if(BUILD_SHARED_LIBS)
	set(STORMBYTE_CRYPTO_MODE shared)
else()
	set(STORMBYTE_CRYPTO_MODE static)
endif()

buildmaster_component(
	StormByte-Crypto
	"StormByte Suite - Crypto"
	"${STORMBYTE_CRYPTO_URL}"
	""
	"${STORMBYTE_CRYPTO_MODE}"
	"StormByte-Crypto"
	"${STORMBYTE_CRYPTO_OPTSTR}"
)
buildmaster_link(StormByte-Crypto StormByte-Base StormByte-Buffer StormByte-Logger StormByte-System)

if(WIN32 AND BUILD_SHARED_LIBS)
	target_compile_definitions(StormByte-Crypto INTERFACE STORMBYTE_CRYPTO_SHARED)
endif()
