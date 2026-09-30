#include <StormByte/network/exception.hxx>

using namespace StormByte::Network;

Exception::~Exception() noexcept = default;

ConnectionError::~ConnectionError() noexcept = default;

ConnectionClosed::~ConnectionClosed() noexcept = default;

PacketError::~PacketError() noexcept = default;

FrameError::~FrameError() noexcept = default;