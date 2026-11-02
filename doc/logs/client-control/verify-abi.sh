#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
work="$root/.validation/abi-reproduction"
mkdir -p "$work/compiler-work" "$work/libstdcxx" "$work/libcxx"
export TMPDIR="$work/compiler-work"

gh release download 2.2.3 --repo StormByte-Suite/StormByte \
	--pattern 'StormByte-2.2.3-linux-x64-libstdc++.zip' \
	--pattern 'StormByte-2.2.3-linux-x64-libc++.zip' --dir "$work" --clobber
(
	cd "$work"
	printf '%s\n' \
		'f637abba35f8dfb81faf7cd6ed7a29ea22b8f6fc7bb232dd157936f2131d4269  StormByte-2.2.3-linux-x64-libstdc++.zip' \
		'0e2aea8b6365ea95158b1fa2be27215e805fe89b750a9dc34ab48b0ebe2e8a18  StormByte-2.2.3-linux-x64-libc++.zip' |
		sha256sum --check
)
unzip -oq "$work/StormByte-2.2.3-linux-x64-libstdc++.zip" -d "$work/libstdcxx"
unzip -oq "$work/StormByte-2.2.3-linux-x64-libc++.zip" -d "$work/libcxx"
sed -n '1,/^ \*\//p' lib/public/StormByte/network/client.cxx > "$work/probe.cxx"
cat >> "$work/probe.cxx" <<'CPP'
#include <StormByte/byte_size.hxx>

long Convert(StormByte::ByteSize value) {
	return static_cast<long>(value);
}

int main(int argc, char**) {
	return Convert(StormByte::ByteSize{argc}) == argc ? 0 : 1;
}
CPP

probe() {
	local label=$1 compiler=$2 runtime=$3
	shift 3
	local prefix="$work/$runtime"
	if "$compiler" -std=c++26 "$@" "$work/probe.cxx" \
		-I"$prefix/include" -L"$prefix/lib" -Wl,-rpath,"$prefix/lib" \
		-lStormByte -o "$work/$label" > "$work/$label.log" 2>&1; then
		"$work/$label"
		printf '%s: link and execution PASS\n' "$label"
	else
		printf '%s: link FAIL (see %s)\n' "$label" "$work/$label.log"
	fi
}

g++ --version | head -1
clang++ --version | head -1
probe gcc-O0 g++ libstdcxx -O0
probe gcc-O2 g++ libstdcxx -O2
probe gcc-O0-asan g++ libstdcxx -O0 -fsanitize=address,undefined
probe clang-O0-libstdcxx clang++ libstdcxx -O0 -stdlib=libstdc++
probe clang-O0-libcxx clang++ libcxx -O0 -stdlib=libc++

g++ -std=c++26 -O0 -I"$work/libstdcxx/include" -c "$work/probe.cxx" -o "$work/gcc.o"
clang++ -std=c++26 -stdlib=libstdc++ -O0 -I"$work/libstdcxx/include" \
	-c "$work/probe.cxx" -o "$work/clang.o"
printf '\nGCC references:\n'
nm -u "$work/gcc.o"
printf '\nClang references:\n'
nm -u "$work/clang.o"
printf '\nReleased ByteSize conversion-template exports:\n'
nm -D --defined-only "$work/libstdcxx/lib/libStormByte.so" | grep 'ByteSizecvT'
