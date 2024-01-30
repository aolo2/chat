#!/bin/sh
set -eu
cd "$(dirname "$(readlink -f "$0")")"

debug="0"
release="0"
rebuild_toolchain="0"

for argument in "$@"; do eval "$argument=1"; done
[ "$debug" != 1 ] && [ "$release" != 1 ] && debug="1"

[ "$debug" = "1" ]   && release="0" && echo [debug build]
[ "$release" = "1" ] && debug="0"   && echo [release build]

toolchain=$PWD/toolchain
code=$PWD/src
build=$PWD/build

if [ ! -e "$toolchain"/.done ] || [ "$rebuild_toolchain" = "1" ]; then
	sources="$toolchain"/sources
	mkdir -p "$sources"
	cd "$sources"

	openssl_version=1.1.1
	curl_version=8.2.1
	ecec_version=9c51ad6b959bc775b6a99018a43803d090ed9f05
	liburing_version=2.4
	musl_toolchain_version=11

	[ ! -e openssl-"$openssl_version".tar.gz ]               && curl -sfLo openssl-"$openssl_version".tar.gz.tmp               https://www.openssl.org/source/openssl-"$openssl_version"v.tar.gz
	[ ! -e curl-"$curl_version".tar.gz ]                     && curl -sfLo curl-"$curl_version".tar.gz.tmp                     https://curl.se/download/curl-"$curl_version".tar.gz
	[ ! -e ecec-"$ecec_version".tar.gz ]                     && curl -sfLo ecec-"$ecec_version".tar.gz.tmp                     https://github.com/web-push-libs/ecec/archive/"$ecec_version".tar.gz
	[ ! -e liburing-"$liburing_version".tar.gz ]             && curl -sfLo liburing-"$liburing_version".tar.gz.tmp             https://github.com/axboe/liburing/archive/refs/tags/liburing-"$liburing_version".tar.gz
	[ ! -e musl-toolchain-"$musl_toolchain_version".tar.gz ] && curl -sfLo musl-toolchain-"$musl_toolchain_version".tar.gz.tmp http://more.musl.cc/"$musl_toolchain_version"/x86_64-linux-musl/x86_64-linux-musl-native.tgz
	find . -name "*.tmp" -type f -exec sh -c 'mv "$1" "${1%.tmp}"' sh {} \;

	cd "$toolchain"

	[ -e musl-libs ]                && rm -r musl-libs
	[ -e native-libs ]              && rm -r native-libs
	[ -e x86_64-linux-musl-native ] && rm -r x86_64-linux-musl-native

	mkdir -p musl-libs
	mkdir -p native-libs
	tar xf "$sources"/musl-toolchain-"$musl_toolchain_version".tar.gz

	muslcc="$toolchain"/x86_64-linux-musl-native/bin/x86_64-linux-musl-gcc

	mkdir -p "$build"
	cd "$build"

	MAKEFLAGS="-j$(nproc) -l$(nproc)"
	export MAKEFLAGS

	[ -e openssl ]  && rm -r openssl
	[ -e curl ]     && rm -r curl
	[ -e ecec ]     && rm -r ecec
	[ -e liburing ] && rm -r liburing

	tar xf "$sources"/openssl-"$openssl_version".tar.gz   --one-top-level=openssl  --strip-components 1
	tar xf "$sources"/curl-"$curl_version".tar.gz         --one-top-level=curl     --strip-components 1
	tar xf "$sources"/ecec-"$ecec_version".tar.gz         --one-top-level=ecec     --strip-components 1
	tar xf "$sources"/liburing-"$liburing_version".tar.gz --one-top-level=liburing --strip-components 1

	cd "$build"/openssl
	LDFLAGS="-Wl,-rpath,\\\$\$ORIGIN" ./config
	make
	cp ./*.so* "$toolchain"/native-libs/
	make clean
	CC="$muslcc" ./Configure linux-x86_64
	make
	cp ./*.a "$toolchain"/musl-libs/

	cd "$build"/curl
	LD_LIBRARY_PATH="$toolchain"/native-libs CPPFLAGS="-I$build/openssl/include" LDFLAGS="-L$toolchain/native-libs -Wl,-rpath,\\\$\$ORIGIN" ./configure --with-openssl --disable-ldap
	LD_LIBRARY_PATH="$toolchain"/native-libs make
	cp lib/.libs/*.so* "$toolchain"/native-libs/
	make clean
	CC="$muslcc" CPPFLAGS="-I$build/openssl/include" LDFLAGS="-static -L$toolchain"/musl-libs ./configure --with-openssl --disable-ldap -disable-shared --enable-static
	make
	cp lib/.libs/*.a "$toolchain"/musl-libs/

	cd "$build"/ecec
	gcc -Iinclude/ -I"$build"/curl/include -I"$build"/openssl/include -L"$toolchain"/native-libs -Wl,-rpath,'$ORIGIN' src/*.c -shared -o libecec.so -lcrypto
	cp libecec.so "$toolchain"/native-libs
	mkdir -p build
	cd build
	CC="$muslcc" OPENSSL_ROOT_DIR="$build"/openssl cmake ..
	make
	cp libece.a "$toolchain"/musl-libs/libecec.a

	cd "$build"/liburing
	./configure
	make
	cp src/liburing.so.2.4 "$toolchain"/native-libs
	ln -sr "$toolchain"/native-libs/liburing.so.2.4 "$toolchain"/native-libs/liburing.so.2
	ln -sr "$toolchain"/native-libs/liburing.so.2 "$toolchain"/native-libs/liburing.so
	make clean
	./configure --cc="$muslcc"
	make
	cp src/liburing.a "$toolchain"/musl-libs

	touch "$toolchain"/.done
fi

warning_flags="-Werror -Wall -Wextra -Wshadow -Wno-unused-function -Wno-unused-result -Wno-deprecated-declarations"
include_flags="-I$build/liburing/src/include -I$build/openssl/include -I$build/curl/include -I$build/ecec/include"

debug_compile_flags="-O0 -ggdb3 -std=gnu11 $include_flags $warning_flags"
debug_linker_flags="-L$toolchain/native-libs -Wl,-rpath,\$ORIGIN/../toolchain/native-libs"

release_compile_flags="-O2 -ggdb3 -std=gnu11 -static $include_flags $warning_flags"
release_linker_flags="-L$toolchain/musl-libs"

[ "$debug" = "1" ]   && compiler="gcc"
[ "$debug" = "1" ]   && compiler_flags="$debug_compile_flags"
[ "$debug" = "1" ]   && linker_flags="$debug_linker_flags"
[ "$debug" = "1" ]   && suffix="debug"
[ "$release" = "1" ] && compiler="$toolchain/x86_64-linux-musl-native/bin/x86_64-linux-musl-gcc"
[ "$release" = "1" ] && compiler_flags="$release_compile_flags"
[ "$release" = "1" ] && linker_flags="$release_linker_flags"
[ "$release" = "1" ] && suffix="release"

mkdir -p "$build"
cd "$build"

$compiler $compiler_flags          $code/websocket/main.c    -o ws-server-"$suffix"           $linker_flags -luring -lcrypt &
$compiler $compiler_flags          $code/media/main.c        -o media-server-"$suffix"        $linker_flags -luring -lm &
$compiler $compiler_flags -pthread $code/notification/main.c -o notification-server-"$suffix" $linker_flags -luring -lcurl -lssl -lcrypto -lecec &

wait