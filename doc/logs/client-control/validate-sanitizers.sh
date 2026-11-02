#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
mode=${1:-binary-clang}
work="$root/.validation/reproduce-$mode"
mkdir -p "$work/compiler-work" "$work/logs"
export TMPDIR="$work/compiler-work"
export ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:strict_string_checks=1:alloc_dealloc_mismatch=1
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
flags='-fsanitize=address,undefined -fno-omit-frame-pointer'
compiler=()

case "$mode" in
	binary-clang)
		compiler=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++)
		cxxflags="-stdlib=libstdc++ $flags"
		;;
	source-gcc)
		compiler=(-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++)
		cxxflags="$flags"
		;;
	*)
		printf 'Usage: %s [binary-clang|source-gcc]\n' "$0" >&2
		exit 2
		;;
esac

common=(-DCMAKE_BUILD_TYPE=Release -DENABLE_ASAN=OFF -DENABLE_LTO=OFF
	-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
	"-DCMAKE_CXX_FLAGS=$cxxflags" "-DCMAKE_C_FLAGS=$flags"
	'-DCMAKE_CXX_FLAGS_RELEASE=-O2 -g -DNDEBUG' '-DCMAKE_C_FLAGS_RELEASE=-O2 -g -DNDEBUG'
	'-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
	'-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined')
targets=(ClientControlTest ClientServerTest TransportTest TLSTransportTest HTTPTest RemoteFileTest)
cmake -S . -B "$work/network" -DENABLE_TEST=ON "${compiler[@]}" "${common[@]}" \
	> "$work/logs/configure.log" 2>&1

if [[ "$mode" == source-gcc ]]; then
	mkdir -p "$work/base-source"
	gh api repos/StormByte-Suite/StormByte/tarball/57f5e29a04e0ae3d83edf42c6c3851ea22bcd012 \
		> "$work/base.tar.gz"
	tar -xzf "$work/base.tar.gz" -C "$work/base-source" --strip-components=1
	gh api repos/StormBytePP/sds/tarball/cc59ce0b12e91521476ad54a1e069fb6fd933bbc \
		> "$work/sds.tar.gz"
	tar -xzf "$work/sds.tar.gz" -C "$work/base-source/thirdparty/sds" --strip-components=1
	cmake -S "$work/base-source" -B "$work/base-build" -DENABLE_TEST=OFF \
		"${compiler[@]}" "${common[@]}" \
		"-DCMAKE_INSTALL_PREFIX=$work/network/stormbyte-buildmaster/install" \
		-DCMAKE_INSTALL_LIBDIR=lib64 > "$work/logs/base-configure.log" 2>&1
	cmake --build "$work/base-build" -j"$(nproc)" > "$work/logs/base-build.log" 2>&1
	cmake --build "$work/network" -j"$(nproc)" --target \
		StormByte-Base_install StormByte-Buffer_install StormByte-Crypto_install \
		StormByte-Logger_install StormByte-System_install nghttp2_install \
		> "$work/logs/dependencies.log" 2>&1
	cmake --install "$work/base-build" > "$work/logs/private-install.log" 2>&1
	# All dependencies are prepared. Fast targets prevent BuildMaster from
	# overwriting the private source Base with its released binary archive.
	fast=(StormByte-Network/fast)
	for target in "${targets[@]}"; do
		fast+=("$target/fast")
	done
	cmake --build "$work/network" -j"$(nproc)" --target "${fast[@]}" > "$work/logs/build.log" 2>&1
else
	cmake --build "$work/network" -j"$(nproc)" --target "${targets[@]}" > "$work/logs/build.log" 2>&1
fi

ldd "$work/network/test/ClientControlTest" > "$work/logs/runtime.log"
ctest --test-dir "$work/network/test" --output-on-failure \
	-R '^(ClientControlTest|ClientServerTest|TransportTest|TLSTransportTest|HTTPTest|RemoteFileTest)$' |
	tee "$work/logs/tests.log"
