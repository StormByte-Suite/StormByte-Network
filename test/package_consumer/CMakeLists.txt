cmake_minimum_required(VERSION 3.28)
project(StormByteNetworkPackageConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(PREPEND CMAKE_MODULE_PATH
	"${NETWORK_PREFIX}/share/cmake/Modules"
	"${DEPENDENCY_PREFIX}/share/cmake/Modules"
)
list(PREPEND CMAKE_PREFIX_PATH "${NETWORK_PREFIX}" "${DEPENDENCY_PREFIX}")
set(CMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH FALSE)
set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH FALSE)
set(CMAKE_FIND_USE_CMAKE_SYSTEM_PATH FALSE)
set(CMAKE_FIND_USE_PACKAGE_REGISTRY FALSE)
set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY FALSE)

find_package(StormByte REQUIRED COMPONENTS Network)
find_package(StormByte REQUIRED COMPONENTS Base Buffer Crypto Logger Network System)

foreach(path IN ITEMS "${STORMBYTE_Network_INCLUDE_DIR}" "${STORMBYTE_Network_LIBRARY}")
	cmake_path(IS_PREFIX NETWORK_PREFIX "${path}" NORMALIZE _in_network_prefix)
	if(NOT _in_network_prefix)
		message(FATAL_ERROR "Network was not found in ${NETWORK_PREFIX}: ${path}")
	endif()
endforeach()
foreach(path IN ITEMS
		"${STORMBYTE_LIBRARY}"
		"${STORMBYTE_Buffer_LIBRARY}"
		"${STORMBYTE_Crypto_LIBRARY}"
		"${STORMBYTE_Logger_LIBRARY}"
		"${STORMBYTE_System_LIBRARY}")
	cmake_path(IS_PREFIX DEPENDENCY_PREFIX "${path}" NORMALIZE _in_dependency_prefix)
	if(NOT _in_dependency_prefix)
		message(FATAL_ERROR "StormByte dependency was not found in ${DEPENDENCY_PREFIX}: ${path}")
	endif()
endforeach()

add_executable(network_package_consumer consumer.cxx)
target_link_libraries(network_package_consumer PRIVATE StormByte::Network)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	target_compile_options(network_package_consumer PRIVATE -Wno-changes-meaning)
endif()
set_target_properties(network_package_consumer PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/bin>"
)
