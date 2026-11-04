#include <StormByte/network/typedefs.hxx>

int main() {
	StormByte::Network::DeserializePacketFunction deserialize;
	return deserialize ? 1 : 0;
}
