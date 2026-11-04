cmake_minimum_required(VERSION 3.28)

function(run_checked description)
	execute_process(
		COMMAND ${ARGN}
		RESULT_VARIABLE result
		OUTPUT_VARIABLE output
		ERROR_VARIABLE error
	)
	if(NOT result STREQUAL "0")
		message(FATAL_ERROR "${description} failed (${result}):\n${output}\n${error}")
	endif()
endfunction()

include(ProcessorCount)
ProcessorCount(jobs)
if(NOT jobs GREATER 0)
	message(FATAL_ERROR "Could not determine the available processor count")
endif()

string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef test_id)
set(test_root "${NETWORK_BUILD_DIR}/test/package-discovery-${test_id}")
if(EXISTS "${test_root}")
	message(FATAL_ERROR "Package discovery test directory already exists: ${test_root}")
endif()
set(installed_prefix "${test_root}/installed")
set(extracted_prefix "${test_root}/extracted")
set(archive "${test_root}/network.zip")
file(MAKE_DIRECTORY "${installed_prefix}" "${extracted_prefix}")

set(config_args)
if(BUILD_CONFIG)
	list(APPEND config_args --config "${BUILD_CONFIG}")
endif()
run_checked("Install Network" "${CMAKE_COMMAND}" --install "${NETWORK_BUILD_DIR}"
	--prefix "${installed_prefix}" ${config_args})
run_checked("Create Network release ZIP" "${CMAKE_COMMAND}" -E chdir "${installed_prefix}"
	"${CMAKE_COMMAND}" -E tar cf "${archive}" --format=zip -- .)
run_checked("Extract Network release ZIP" "${CMAKE_COMMAND}" -E chdir "${extracted_prefix}"
	"${CMAKE_COMMAND}" -E tar xf "${archive}")

set(generator_args -G "${GENERATOR}")
if(GENERATOR_PLATFORM)
	list(APPEND generator_args -A "${GENERATOR_PLATFORM}")
endif()
if(GENERATOR_TOOLSET)
	list(APPEND generator_args -T "${GENERATOR_TOOLSET}")
endif()
set(compiler_args
	"-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
	"-DCMAKE_CXX_FLAGS=${CXX_FLAGS}"
	"-DCMAKE_EXE_LINKER_FLAGS=${EXE_LINKER_FLAGS}"
	"-DCMAKE_BUILD_TYPE=${BUILD_CONFIG}"
)
if(MAKE_PROGRAM)
	list(APPEND compiler_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(CXX_COMPILER_TARGET)
	list(APPEND compiler_args "-DCMAKE_CXX_COMPILER_TARGET=${CXX_COMPILER_TARGET}")
endif()
if(OSX_ARCHITECTURES)
	string(REPLACE ";" "\\;" escaped_architectures "${OSX_ARCHITECTURES}")
	list(APPEND compiler_args "-DCMAKE_OSX_ARCHITECTURES=${escaped_architectures}")
endif()
if(OSX_SYSROOT)
	list(APPEND compiler_args "-DCMAKE_OSX_SYSROOT=${OSX_SYSROOT}")
endif()

foreach(kind IN ITEMS installed extracted)
	set(prefix "${${kind}_prefix}")
	set(consumer_build "${test_root}/consumer-${kind}")
	run_checked("Configure consumer from ${kind} Network" "${CMAKE_COMMAND}"
		-S "${CONSUMER_SOURCE_DIR}" -B "${consumer_build}" ${generator_args}
		"-DNETWORK_PREFIX=${prefix}" "-DDEPENDENCY_PREFIX=${DEPENDENCY_PREFIX}"
		${compiler_args})
	run_checked("Build consumer from ${kind} Network" "${CMAKE_COMMAND}"
		--build "${consumer_build}" ${config_args} --parallel "${jobs}")

	if(CMAKE_HOST_WIN32)
		set(ENV{PATH} "${prefix}/bin;${DEPENDENCY_PREFIX}/bin;$ENV{PATH}")
	else()
		set(library_paths "${prefix}/lib:${prefix}/lib64:${DEPENDENCY_PREFIX}/lib:${DEPENDENCY_PREFIX}/lib64")
		set(ENV{LD_LIBRARY_PATH} "${library_paths}:$ENV{LD_LIBRARY_PATH}")
		set(ENV{DYLD_LIBRARY_PATH} "${library_paths}:$ENV{DYLD_LIBRARY_PATH}")
	endif()
	run_checked("Run consumer from ${kind} Network"
		"${consumer_build}/bin/network_package_consumer${EXECUTABLE_SUFFIX}")
endforeach()

file(REMOVE_RECURSE "${test_root}")
