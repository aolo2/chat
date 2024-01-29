#!/bin/sh
set -eu

OPENSSL_VERSION=1.1.1
CURL_VERSION=8.2.1
ECEC_VERSION=9c51ad6b959bc775b6a99018a43803d090ed9f05
LIBURING_VERSION=2.4
MUSL_TOOLCHAIN_VERSION=11

MAKEFLAGS="-j$(nproc) -l$(nproc)"
export MAKEFLAGS

PROJECT_DIR=$(readlink -f "$(dirname "$(readlink -f "$0")")"/..)
TOOLCHAIN_DIR="$PROJECT_DIR"/toolchain
SOURCES_DIR="$TOOLCHAIN_DIR"/sources
BUILD_DIR="$PROJECT_DIR"/build

[ -e "$TOOLCHAIN_DIR"/.done ] && echo "Toolchain already built. Skipping." && exit 0

mkdir -p "$SOURCES_DIR"
cd "$SOURCES_DIR"

[ ! -e openssl-"$OPENSSL_VERSION".tar.gz ]               && curl -sfLo openssl-"$OPENSSL_VERSION".tar.gz.tmp               https://www.openssl.org/source/openssl-"$OPENSSL_VERSION"v.tar.gz
[ ! -e curl-"$CURL_VERSION".tar.gz ]                     && curl -sfLo curl-"$CURL_VERSION".tar.gz.tmp                     https://curl.se/download/curl-"$CURL_VERSION".tar.gz
[ ! -e ecec-"$ECEC_VERSION".tar.gz ]                     && curl -sfLo ecec-"$ECEC_VERSION".tar.gz.tmp                     https://github.com/web-push-libs/ecec/archive/"$ECEC_VERSION".tar.gz
[ ! -e liburing-"$LIBURING_VERSION".tar.gz ]             && curl -sfLo liburing-"$LIBURING_VERSION".tar.gz.tmp             https://github.com/axboe/liburing/archive/refs/tags/liburing-"$LIBURING_VERSION".tar.gz
[ ! -e musl-toolchain-"$MUSL_TOOLCHAIN_VERSION".tar.gz ] && curl -sfLo musl-toolchain-"$MUSL_TOOLCHAIN_VERSION".tar.gz.tmp http://more.musl.cc/"$MUSL_TOOLCHAIN_VERSION"/x86_64-linux-musl/x86_64-linux-musl-native.tgz
find . -name "*.tmp" -type f -exec sh -c 'mv "$1" "${1%.tmp}"' sh {} \;

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

[ -e openssl ]  && rm -r openssl
[ -e curl ]     && rm -r curl
[ -e ecec ]     && rm -r ecec
[ -e liburing ] && rm -r liburing

tar xf "$SOURCES_DIR"/openssl-"$OPENSSL_VERSION".tar.gz   --one-top-level=openssl  --strip-components 1
tar xf "$SOURCES_DIR"/curl-"$CURL_VERSION".tar.gz         --one-top-level=curl     --strip-components 1
tar xf "$SOURCES_DIR"/ecec-"$ECEC_VERSION".tar.gz         --one-top-level=ecec     --strip-components 1
tar xf "$SOURCES_DIR"/liburing-"$LIBURING_VERSION".tar.gz --one-top-level=liburing --strip-components 1

mkdir -p "$TOOLCHAIN_DIR"
cd "$TOOLCHAIN_DIR"

MUSL_LIBS_DIR="$TOOLCHAIN_DIR"/musl-libs
NATIVE_LIBS_DIR="$TOOLCHAIN_DIR"/native-libs
MUSL_TOOLCHAIN_DIR="$TOOLCHAIN_DIR"/x86_64-linux-musl-native
MUSL_CC="$MUSL_TOOLCHAIN_DIR"/bin/x86_64-linux-musl-gcc

[ -e "$MUSL_LIBS_DIR" ]      && rm -r "$MUSL_LIBS_DIR"
[ -e "$NATIVE_LIBS_DIR" ]    && rm -r "$NATIVE_LIBS_DIR"
[ -e "$MUSL_TOOLCHAIN_DIR" ] && rm -r "$MUSL_TOOLCHAIN_DIR"

tar xf "$SOURCES_DIR"/musl-toolchain-"$MUSL_TOOLCHAIN_VERSION".tar.gz

mkdir -p "$MUSL_LIBS_DIR"
mkdir -p "$NATIVE_LIBS_DIR"

# native libs

cd "$BUILD_DIR"/openssl
./config
make
cp ./*.so* "$NATIVE_LIBS_DIR"

cd "$BUILD_DIR"/curl
LD_LIBRARY_PATH="$NATIVE_LIBS_DIR" CPPFLAGS="-I$BUILD_DIR/openssl/include" LDFLAGS="-L$NATIVE_LIBS_DIR" ./configure --with-openssl --disable-ldap
LD_LIBRARY_PATH="$NATIVE_LIBS_DIR" make
cp lib/.libs/*.so* "$NATIVE_LIBS_DIR"

cd "$BUILD_DIR"/ecec
gcc -Iinclude/ -I"$BUILD_DIR"/curl/include -L"$BUILD_DIR"/curl/lib/.libs/ -I"$BUILD_DIR"/openssl/include -L"$NATIVE_LIBS_DIR" src/*.c -shared -o libecec.so -lcrypto
cp libecec.so "$NATIVE_LIBS_DIR"

cd "$BUILD_DIR"/liburing
./configure
make
cp src/liburing.so.2.4 "$NATIVE_LIBS_DIR"
cd "$NATIVE_LIBS_DIR"
ln -s liburing.so.2.4 liburing.so.2
ln -s liburing.so.2 liburing.so

# musl libs

cd "$BUILD_DIR"/openssl
make clean
CC="$MUSL_CC" ./Configure linux-x86_64
make
cp ./*.a "$MUSL_LIBS_DIR"

cd "$BUILD_DIR"/curl
make clean
CC="$MUSL_CC" LD_LIBRARY_PATH="$MUSL_LIBS_DIR" CPPFLAGS="-I$BUILD_DIR/openssl/include" LDFLAGS="-static -L$MUSL_LIBS_DIR" ./configure --with-openssl --disable-ldap -disable-shared --enable-static
LD_LIBRARY_PATH="$MUSL_LIBS_DIR" make
cp lib/.libs/*.a "$MUSL_LIBS_DIR"

cd "$BUILD_DIR"/ecec
mkdir -p build
cd build
CC="$MUSL_CC" OPENSSL_ROOT_DIR="$BUILD_DIR"/openssl cmake ..
make
cp libece.a "$MUSL_LIBS_DIR"

cd "$BUILD_DIR"/liburing
make clean
./configure --cc="$MUSL_CC"
make
cp src/liburing.a "$MUSL_LIBS_DIR"

touch "$TOOLCHAIN_DIR"/.done
