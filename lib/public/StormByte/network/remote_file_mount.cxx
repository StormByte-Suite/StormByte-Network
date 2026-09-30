#include <StormByte/network/remote_file_mount.hxx>

#include <algorithm>

namespace StormByte::Network {
	RemoteFileMount::~RemoteFileMount() noexcept = default;

	RemoteFileMount::RemoteFileMount(Status status, ChannelToken token, std::uint16_t port,
		std::uint16_t maximum_timeout_seconds, Access access) noexcept:
		m_status(status), m_token(std::move(token)), m_port(port),
		m_maximum_timeout_seconds(maximum_timeout_seconds), m_access(access) {}

	RemoteFileMount RemoteFileMount::NotAuthorized() noexcept {
		return RemoteFileMount{Status::NotAuthorized, {}, 0, 0, Access::None};
	}

	RemoteFileMount RemoteFileMount::Unavailable() noexcept {
		return RemoteFileMount{Status::Unavailable, {}, 0, 0, Access::None};
	}

	RemoteFileMount RemoteFileMount::FileBeingRead() noexcept {
		return RemoteFileMount{Status::FileBeingRead, {}, 0, 0, Access::None};
	}

	RemoteFileMount RemoteFileMount::FileBeingWritten() noexcept {
		return RemoteFileMount{Status::FileBeingWritten, {}, 0, 0, Access::None};
	}

	RemoteFileMount RemoteFileMount::Failed() noexcept {
		return RemoteFileMount{Status::Failed, {}, 0, 0, Access::None};
	}

	std::uint16_t RemoteFileMount::Port() const noexcept {
		return m_port;
	}

	std::uint16_t RemoteFileMount::MaximumTimeoutSeconds() const noexcept {
		return m_maximum_timeout_seconds;
	}

	RemoteFileMount::Status RemoteFileMount::Result() const noexcept {
		return m_status;
	}

	RemoteFileMount::Access RemoteFileMount::Mode() const noexcept {
		return m_access;
	}

	const RemoteFileMount::ChannelToken& RemoteFileMount::Token() const noexcept {
		return m_token;
	}
}

namespace StormByte::Detail {
	using StormByte::BinaryData;
	using StormByte::ByteSize;
	using StormByte::DeserializeError;
	using StormByte::Expected;
	using StormByte::Network::RemoteFileMount;
	using StormByte::Serializable;
	using StormByte::Unexpected;

	namespace {
		constexpr std::uint32_t mount_magic = 0x5342464Du;
		constexpr std::uint16_t mount_version = 1;
		constexpr ByteSize mount_size = ByteSize{
			sizeof(std::uint32_t) + sizeof(std::uint16_t) + sizeof(std::uint16_t)
				+ sizeof(std::uint16_t) + sizeof(std::uint8_t) + sizeof(std::uint8_t)
				+ RemoteFileMount::ChannelToken{}.size()};
	}

	ByteSize Codec<RemoteFileMount>::Size(const RemoteFileMount&) noexcept {
		return mount_size;
	}

	BinaryData Codec<RemoteFileMount>::Write(const RemoteFileMount& data) noexcept {
		BinaryData result;
		result.reserve(mount_size);
		result.append(Serializable<std::uint32_t>(mount_magic).Serialize());
		result.append(Serializable<std::uint16_t>(mount_version).Serialize());
		result.append(Serializable<std::uint16_t>(data.Port()).Serialize());
		result.append(Serializable<std::uint16_t>(data.MaximumTimeoutSeconds()).Serialize());
		result.append(Serializable<std::uint8_t>(static_cast<std::uint8_t>(data.Result())).Serialize());
		result.append(Serializable<std::uint8_t>(static_cast<std::uint8_t>(data.Mode())).Serialize());
		result.append(std::span<const std::byte>{data.Token()});
		return result;
	}

	Expected<RemoteFileMount, DeserializeError> Codec<RemoteFileMount>::Read(
		std::span<const std::byte> data) noexcept {
		if (data.size() != mount_size) {
			return Unexpected<DeserializeError>("Invalid remote file mount payload size");
		}

		std::size_t offset = 0;
		const auto read_u32 = [&data, &offset]() {
			auto value = Serializable<std::uint32_t>::Deserialize(data.subspan(offset, sizeof(std::uint32_t)));
			offset += sizeof(std::uint32_t);
			return value;
		};
		const auto read_u16 = [&data, &offset]() {
			auto value = Serializable<std::uint16_t>::Deserialize(data.subspan(offset, sizeof(std::uint16_t)));
			offset += sizeof(std::uint16_t);
			return value;
		};
		const auto read_u8 = [&data, &offset]() {
			auto value = Serializable<std::uint8_t>::Deserialize(data.subspan(offset, sizeof(std::uint8_t)));
			offset += sizeof(std::uint8_t);
			return value;
		};

		const auto magic = read_u32();
		const auto version = read_u16();
		const auto port = read_u16();
		const auto maximum_timeout_seconds = read_u16();
		const auto status = read_u8();
		const auto access = read_u8();
		if (!magic || !version || !port || !maximum_timeout_seconds || !status || !access
			|| *magic != mount_magic || *version != mount_version) {
			return Unexpected<DeserializeError>("Invalid remote file mount descriptor");
		}

		RemoteFileMount::ChannelToken token{};
		std::ranges::copy(data.subspan(offset), token.begin());
		const auto status_value = static_cast<RemoteFileMount::Status>(*status);
		const auto access_value = static_cast<RemoteFileMount::Access>(*access);
		const bool empty_token = std::ranges::all_of(token,
			[](const std::byte value) { return value == std::byte{0}; });
		if (status_value == RemoteFileMount::Status::Authorized) {
			if (*port == 0 || *maximum_timeout_seconds < 3 || *maximum_timeout_seconds > 3600 || empty_token
				|| (access_value != RemoteFileMount::Access::Read
				&& access_value != RemoteFileMount::Access::Write)) {
				return Unexpected<DeserializeError>("Invalid authorized remote file mount descriptor");
			}
		} else if ((status_value != RemoteFileMount::Status::NotAuthorized
				&& status_value != RemoteFileMount::Status::Unavailable
				&& status_value != RemoteFileMount::Status::FileBeingRead
				&& status_value != RemoteFileMount::Status::FileBeingWritten
				&& status_value != RemoteFileMount::Status::Failed)
			|| *port != 0 || *maximum_timeout_seconds != 0 || !empty_token
			|| access_value != RemoteFileMount::Access::None) {
			return Unexpected<DeserializeError>("Invalid rejected remote file mount descriptor");
		}

		if (status_value == RemoteFileMount::Status::NotAuthorized) {
			return RemoteFileMount::NotAuthorized();
		}
		if (status_value == RemoteFileMount::Status::Unavailable) {
			return RemoteFileMount::Unavailable();
		}
		if (status_value == RemoteFileMount::Status::FileBeingRead) {
			return RemoteFileMount::FileBeingRead();
		}
		if (status_value == RemoteFileMount::Status::FileBeingWritten) {
			return RemoteFileMount::FileBeingWritten();
		}
		if (status_value == RemoteFileMount::Status::Failed) {
			return RemoteFileMount::Failed();
		}
		return RemoteFileMount{RemoteFileMount::Status::Authorized, std::move(token), *port,
			*maximum_timeout_seconds, access_value};
	}
}