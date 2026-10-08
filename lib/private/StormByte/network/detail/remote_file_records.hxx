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

#pragma once

#include <StormByte/network/remote_file_mount.hxx>
#include <StormByte/network/visibility.h>
#include <StormByte/safe/mutex.hxx>
#include <StormByte/safe/pointers.hxx>
#include <StormByte/safe/string.hxx>

/**
 * @namespace StormByte::Network::Detail::RemoteFile
 * @brief Private server endpoint for one application-authorized file mount.
 */
namespace StormByte::Network::Detail::RemoteFile {
	/**
	 * @brief Network-owned native file backend; not MaybeSafe-certified.
	 */
	class FileStream;

	/**
	 * @struct MountedFile
	 * @brief Synchronized descriptor for one server-local mounted path.
	 * @details Construction and destruction run out-of-line in Network. Native
	 * backend allocation uses Base's heap in Network; its Safe unique owner
	 * invokes a Network-defined destruction operation. Copying, movement and assignment are
	 * disabled so neither native ownership nor the Base-owned mutex can migrate.
	 * Only Base-heap shared handles are copied into the registry. Network and Base
	 * must remain loaded with a compatible ABI until the final handle is released.
	 */
	struct STORMBYTE_NETWORK_PRIVATE MountedFile final {
		/**
		 * @brief Create an empty descriptor and its Base-owned mutex in Network.
		 */
		MountedFile();

		/**
		 * @brief Copy construction is disabled for unique synchronized state.
		 * @param other Descriptor that cannot be copied.
		 */
		MountedFile(const MountedFile& other) = delete;

		/**
		 * @brief Move construction is disabled for synchronized state.
		 * @param other Descriptor that cannot be moved.
		 */
		MountedFile(MountedFile&& other) = delete;

		/**
		 * @brief Release native ownership in Network and Safe state through Base.
		 * @pre No file operation holds the mutex at final release.
		 */
		~MountedFile() noexcept;

		/**
		 * @brief Copy assignment is disabled for unique synchronized state.
		 * @param other Descriptor that cannot be copied.
		 * @return This descriptor (operation is deleted).
		 */
		MountedFile& operator=(const MountedFile& other) = delete;

		/**
		 * @brief Move assignment is disabled for synchronized state.
		 * @param other Descriptor that cannot be moved.
		 * @return This descriptor (operation is deleted).
		 */
		MountedFile& operator=(MountedFile&& other) = delete;

		Safe::String path_key;			///< Base-owned UTF-8 registry path.
		RemoteFileMount::Access access{RemoteFileMount::Access::None};	///< Shared read or exclusive write mode.
		Safe::Unique<FileStream> backend;	///< Base-heap native ownership managed only in Network.
		Safe::Mutex mutex;				///< Base-owned file-operation gate.
	};

	/**
	 * @struct MountEntry
	 * @brief One capability's access and open state.
	 * @details Aggregate construction, implicit copying, movement, assignment and
	 * destruction operate only on scalar state and a Base-heap shared handle.
	 * Final descriptor destruction calls Network's stored provider operation.
	 * Network and Base must remain loaded with a compatible ABI until release.
	 */
	struct MountEntry final {
		RemoteFileMount::Access access{RemoteFileMount::Access::None};	///< Granted access mode.
		Safe::Shared<MountedFile> file;	///< Shared mounted descriptor.
		bool opened{false};				///< Whether this capability was opened.
	};
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Detail::RemoteFile::MountedFile);

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Detail::RemoteFile::MountEntry);