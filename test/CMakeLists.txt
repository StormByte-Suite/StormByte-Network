option(ENABLE_TEST "Enable Unit Tests" OFF)
option(ENABLE_CI_FIXTURES "Enable CI-only package discovery fixtures" OFF)
if(ENABLE_TEST)
	enable_testing()

	add_test(NAME OptimizationTests
		COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_SOURCE_DIR}/optimizations_test.cmake")
	set_tests_properties(OptimizationTests PROPERTIES TIMEOUT 15)
	add_executable(ClientControlTest client_control_test.cxx)
	target_link_libraries(ClientControlTest PRIVATE StormByte::Network)
	target_compile_definitions(ClientControlTest PRIVATE
		TLS_FIXTURE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tls")
	if(WIN32)
		target_link_libraries(ClientControlTest PRIVATE ws2_32)
	endif()
	add_test(NAME ClientControlTest COMMAND ClientControlTest)
	set_tests_properties(ClientControlTest PROPERTIES TIMEOUT 30)

	add_executable(HTTPTest
		http_test.cxx
		../lib/private/StormByte/network/connection/handler.cxx
		../lib/private/StormByte/network/connection/info.cxx
		../lib/private/StormByte/network/connection/resolver.cxx
		../lib/private/StormByte/network/socket/client.cxx
		../lib/private/StormByte/network/socket/operation.cxx
		../lib/private/StormByte/network/socket/server.cxx
		../lib/private/StormByte/network/socket/socket.cxx
		../lib/private/StormByte/network/socket/tls.cxx
	)
	get_property(_http_nghttp2 GLOBAL PROPERTY STORMBYTE_NETWORK_NGHTTP2_TARGET)
	target_link_libraries(HTTPTest PRIVATE StormByte::Network "${_http_nghttp2}")
	target_compile_definitions(HTTPTest PRIVATE
		TLS_FIXTURE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tls")
	if(WIN32)
		target_link_libraries(HTTPTest PRIVATE ws2_32 iphlpapi)
	endif()
	add_test(NAME HTTPTest COMMAND HTTPTest)
	set_tests_properties(HTTPTest PROPERTIES TIMEOUT 90)
	# Private symbols are hidden in the shared library. Compile the transport
	# provider locally rather than exporting internals or requiring static archives.
	add_executable(TransportTest
		transport_test.cxx
		../lib/private/StormByte/network/connection/handler.cxx
		../lib/private/StormByte/network/connection/info.cxx
		../lib/private/StormByte/network/connection/resolver.cxx
		../lib/private/StormByte/network/socket/client.cxx
		../lib/private/StormByte/network/socket/operation.cxx
		../lib/private/StormByte/network/socket/socket.cxx
		../lib/private/StormByte/network/socket/tls.cxx
	)
	target_link_libraries(TransportTest StormByte::Network)
	if(WIN32)
		target_link_libraries(TransportTest ws2_32 iphlpapi)
	endif()
	add_test(NAME TransportTest COMMAND TransportTest)
	set_tests_properties(TransportTest PROPERTIES TIMEOUT 30)

	add_executable(TLSTransportTest
		tls_transport_test.cxx
		../lib/private/StormByte/network/connection/handler.cxx
		../lib/private/StormByte/network/connection/info.cxx
		../lib/private/StormByte/network/connection/resolver.cxx
		../lib/private/StormByte/network/socket/client.cxx
		../lib/private/StormByte/network/socket/operation.cxx
		../lib/private/StormByte/network/socket/socket.cxx
		../lib/private/StormByte/network/socket/tls.cxx
	)
	target_link_libraries(TLSTransportTest StormByte::Network)
	target_compile_definitions(TLSTransportTest PRIVATE
		TLS_FIXTURE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tls")
	if(WIN32)
		target_link_libraries(TLSTransportTest ws2_32 iphlpapi)
	endif()
	add_test(NAME TLSTransportTest COMMAND TLSTransportTest)
	set_tests_properties(TLSTransportTest PROPERTIES TIMEOUT 30)

	foreach(test_class IN ITEMS
		Client
		ClientTelemetry
		ConnectionClosed
		ConnectionError
		DeserializePacketFunction
		Endpoint
		Exception
		FrameError
		Packet
		PacketError
		RemoteFileMount
		Server
		ServerTelemetry
		Telemetry
	)
		string(REGEX REPLACE "([a-z])([A-Z])" "\\1_\\2" test_source "${test_class}")
		string(TOLOWER "${test_source}" test_source)
		add_executable(${test_class}SafeTest ${test_source}_safe_test.cxx)
		target_link_libraries(${test_class}SafeTest StormByte::Network)
		if(WIN32)
			target_link_libraries(${test_class}SafeTest ws2_32)
		endif()
		add_test(NAME ${test_class}SafeTest COMMAND ${test_class}SafeTest)
		set_tests_properties(${test_class}SafeTest PROPERTIES TIMEOUT 30)
	endforeach()


	add_executable(DiscoveryTest discovery_test.cxx)
	target_link_libraries(DiscoveryTest StormByte::Network)
	if(WIN32)
		target_link_libraries(DiscoveryTest ws2_32)
	endif()
	add_test(NAME DiscoveryTest COMMAND DiscoveryTest)
	set_tests_properties(DiscoveryTest PROPERTIES TIMEOUT 30)

	# Combined server/client socket test (multi-client)
	# It is a private class, keep for internal testing only
	# add_executable(SocketMultiTest socket_multi_test.cxx)
	# target_link_libraries(SocketMultiTest StormByte::Network)
	# add_test(NAME SocketMultiTest COMMAND SocketMultiTest)

	# Client/Server interaction test
	add_executable(ClientServerTest client_server_test.cxx)
	target_link_libraries(ClientServerTest StormByte::Network)
	target_compile_definitions(ClientServerTest PRIVATE TLS_FIXTURE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tls")
	if(WIN32)
		target_link_libraries(ClientServerTest ws2_32)
	endif()
	add_test(NAME ClientServerTest COMMAND ClientServerTest)
	set_tests_properties(ClientServerTest PROPERTIES TIMEOUT 300)

	add_executable(RemoteFileTest remote_file_test.cxx)
	target_link_libraries(RemoteFileTest StormByte::Network)
	target_compile_definitions(RemoteFileTest PRIVATE TLS_FIXTURE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tls")
	if(WIN32)
		target_link_libraries(RemoteFileTest ws2_32)
	endif()
	add_test(NAME RemoteFileTest COMMAND RemoteFileTest)
	set_tests_properties(RemoteFileTest PROPERTIES TIMEOUT 300)

	if(ENABLE_CI_FIXTURES AND BUILD_SHARED_LIBS AND WITH_STORMBYTE STREQUAL "BUNDLED" AND NOT CMAKE_CROSSCOMPILING)
		set(_package_cxx_flags "${CMAKE_CXX_FLAGS}")
		set(_package_link_flags "${CMAKE_EXE_LINKER_FLAGS}")
		if(COMMAND _pgo_strip_profile_flags)
			_pgo_strip_profile_flags("${_package_cxx_flags}" _package_cxx_flags)
			_pgo_strip_profile_flags("${_package_link_flags}" _package_link_flags)
		endif()

		add_test(NAME PackageDiscoveryTests
			COMMAND "${CMAKE_COMMAND}"
				"-DNETWORK_BUILD_DIR=${PROJECT_BINARY_DIR}"
				"-DCONSUMER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/package_consumer"
				"-DDEPENDENCY_PREFIX=${BUILDMASTER_INSTALL_DIR}"
				"-DBUILD_CONFIG=$<CONFIG>"
				"-DGENERATOR=${CMAKE_GENERATOR}"
				"-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
				"-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
				"-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
				"-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
				"-DCXX_COMPILER_TARGET=${CMAKE_CXX_COMPILER_TARGET}"
				"-DCXX_FLAGS=${_package_cxx_flags}"
				"-DEXE_LINKER_FLAGS=${_package_link_flags}"
				"-DOSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}"
				"-DOSX_SYSROOT=${CMAKE_OSX_SYSROOT}"
				"-DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
				-P "${CMAKE_CURRENT_SOURCE_DIR}/package_discovery_test.cmake")
		set_tests_properties(PackageDiscoveryTests PROPERTIES TIMEOUT 180)
	endif()
endif()
