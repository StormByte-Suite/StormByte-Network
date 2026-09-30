/*
 * Copyright (C) 2024-2026 David C. Manuelda (StormBytePP)
 *
 * This file is part of StormByte-Network.
 *
 * StormByte-Network original source is dual-licensed:
 *
 * 1. GNU Lesser General Public License v3.0 (or later)
 *    You may redistribute and/or modify this file under the terms of the
 *    GNU Lesser General Public License as published by the Free Software
 *    Foundation, either version 3 of the License, or (at your option)
 *    any later version.
 *
 * 2. Commercial license
 *    Alternatively, this file may be used under the terms of a commercial
 *    license agreement with the copyright holder
 *    (David C. Manuelda <StormByte@gmail.com>).
 *
 * Both licenses apply only to original StormByte-Network source in this
 * repository. They do not cover other StormByte modules or any third-party
 * material shipped with this repository (including everything under
 * thirdparty/, and in particular the bundled StormByte Buffer tree), which
 * remains under its own license.
 *
 * Neither license grants any patent rights. Any patent licenses required
 * to use this software or third-party components must be obtained separately
 * from the patent holders.
 *
 * StormByte-Network is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 3 along with StormByte-Network. If not, see
 * <https://www.gnu.org/licenses/lgpl-3.0.html>.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later OR LicenseRef-StormByte-Commercial
 */

#include <StormByte/network/connection/handler.hxx>
#ifdef UNIX
#include <cerrno>		// For errno
#include <cstring>		// For strerror_r
#else
#include <winsock2.h>
#endif
#include <StormByte/string/string.hxx>
#include <StormByte/string/wstring.hxx>
using namespace StormByte::Network::Connection;
Handler::Handler() noexcept {
	#ifdef WINDOWS
	// Initialize Winsock; set initialized=true only on success
	m_initialized = (WSAStartup(MAKEWORD(2, 2), &m_wsaData) == 0);
	#else
	m_initialized = true;
	#endif
}

Handler::~Handler() noexcept {
	#ifdef WINDOWS
	WSACleanup();
	#endif
}

Handler& Handler::Instance() noexcept {
	static Handler instance;
	return instance;
}

std::string Handler::LastError() const noexcept {
	std::string error_string;
	#ifdef WINDOWS
	wchar_t* errorMsg = nullptr;
	DWORD res = FormatMessage(
				FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				nullptr, WSAGetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				reinterpret_cast<LPWSTR>(&errorMsg), 0, nullptr);
	if (res != 0 && errorMsg != nullptr) {
		const StormByte::String::WString wide_message{std::wstring_view{errorMsg}};
		const StormByte::String::String utf8_message{wide_message};
		error_string.assign(static_cast<std::string_view>(utf8_message));
		LocalFree(errorMsg);
	} else {
		// No message available; leave empty so callers can decide how to present it
		if (errorMsg) LocalFree(errorMsg);
	}
	#else
	if (errno != 0)
		error_string = ErrnoToString(errno);
	#endif
	return error_string;
}

int Handler::LastErrorCode() const noexcept {
	#ifdef WINDOWS
	return WSAGetLastError();
	#else
	return errno;
	#endif
}

std::string Handler::ErrnoToString(int errnum) const noexcept {
	#ifdef WINDOWS
	char buf[256] = {0};
	if (strerror_s(buf, sizeof(buf), errnum) == 0) return std::string(buf);
	return std::to_string(errnum);
	#else
	char buf[256] = {0};
	// Handle both GNU (returns char*) and POSIX (returns int) strerror_r variants
	#if defined(__GLIBC__) && !defined(__APPLE__)
	char *msg = strerror_r(errnum, buf, sizeof(buf));
	return std::string(msg ? msg : "Unknown error");
	#else
	if (strerror_r(errnum, buf, sizeof(buf)) == 0) return std::string(buf);
	return std::to_string(errnum);
	#endif
	#endif
}
