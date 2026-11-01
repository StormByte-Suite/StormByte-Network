option(ENABLE_TEST "Enable Unit Tests" OFF)
if(ENABLE_TEST)
	enable_testing()
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

	add_executable(HandshakeTest handshake_test.cxx)
	target_link_libraries(HandshakeTest StormByte::Network)
	if(WIN32)
		target_link_libraries(HandshakeTest ws2_32)
	endif()
	add_test(NAME HandshakeTest COMMAND HandshakeTest)
	set_tests_properties(HandshakeTest PROPERTIES TIMEOUT 30)
	# Combined server/client socket test (multi-client)
	# It is a private class, keep for internal testing only
	# add_executable(SocketMultiTest socket_multi_test.cxx)
	# target_link_libraries(SocketMultiTest StormByte::Network)
	# add_test(NAME SocketMultiTest COMMAND SocketMultiTest)

	# Client/Server interaction test
	add_executable(ClientServerTest client_server_test.cxx)
	target_link_libraries(ClientServerTest StormByte::Network)
	if(WIN32)
		target_link_libraries(ClientServerTest ws2_32)
	endif()
	add_test(NAME ClientServerTest COMMAND ClientServerTest)
	set_tests_properties(ClientServerTest PROPERTIES TIMEOUT 300)

	add_executable(RemoteFileTest remote_file_test.cxx)
	target_link_libraries(RemoteFileTest StormByte::Network)
	if(WIN32)
		target_link_libraries(RemoteFileTest ws2_32)
	endif()
	add_test(NAME RemoteFileTest COMMAND RemoteFileTest)
	set_tests_properties(RemoteFileTest PROPERTIES TIMEOUT 300)
endif()
