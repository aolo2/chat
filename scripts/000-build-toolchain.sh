#!/bin/bash

# apt-get install gcc g++ make cmake git wget

set -e
# set -x

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
TOOLCHAIN_DIR=$SCRIPT_DIR/../toolchain

OPENSSL_DIR=openssl-1.1.1v
CURL_DIR=curl-8.2.1
ECEC_DIR=ecec-master
MUSL_DIR=x86_64-linux-musl-native
LIBURING_DIR=liburing-2.4

GCC_LIBS_DIR=gcc
MUSL_LIBS_DIR=musl

export MAKEFLAGS="-j$(nproc) -l$(nproc)"

DONE_MARKER=.done

mkdir -p $TOOLCHAIN_DIR
pushd $TOOLCHAIN_DIR

if [ -f $DONE_MARKER ]; then
    echo 'Toolchain already built (.done file present). Exiting'
    exit 0
fi

rm -rf $GCC_LIBS_DIR $MUSL_LIBS_DIR $OPENSSL_DIR.tar.gz $CURL_DIR.tar.gz $ECEC_DIR.tar.gz $MUSL_DIR.tar.gz $LIBURING_DIR.tar.gz $OPENSSL_DIR $CURL_DIR $ECEC_DIR $MUSL_DIR $LIBURING_DIR

wget -O $OPENSSL_DIR.tar.gz https://www.openssl.org/source/openssl-1.1.1v.tar.gz
wget -O $CURL_DIR.tar.gz https://curl.se/download/curl-8.2.1.tar.gz
wget -O $ECEC_DIR.tar.gz https://github.com/web-push-libs/ecec/archive/refs/heads/master.tar.gz
wget -O $LIBURING_DIR.tar.gz https://github.com/axboe/liburing/archive/refs/tags/liburing-2.4.tar.gz
wget -O $MUSL_DIR.tar.gz https://musl.cc/x86_64-linux-musl-native.tgz

tar -xf $OPENSSL_DIR.tar.gz
tar -xf $CURL_DIR.tar.gz
tar -xf $ECEC_DIR.tar.gz
tar -xf $LIBURING_DIR.tar.gz
tar -xf $MUSL_DIR.tar.gz

OPENSSL_FULLPATH=$(realpath $OPENSSL_DIR)
CURL_FULLPATH=$(realpath $CURL_DIR)
MUSL_FULLPATH=$(realpath $MUSL_DIR)

mkdir $GCC_LIBS_DIR
mkdir $MUSL_LIBS_DIR

GCC_LIBRARY_PATH=$(realpath $GCC_LIBS_DIR)
MUSL_LIBRARY_PATH=$(realpath $MUSL_LIBS_DIR)

# Build openssl-1.1.1v with gcc
pushd $OPENSSL_DIR
./config
make
cp *.so* $GCC_LIBRARY_PATH
popd

# Build libcurl with gcc (with dynamic openssl compiled by gcc)
pushd $CURL_DIR
LD_LIBRARY_PATH=$GCC_LIBRARY_PATH CPPFLAGS="-I$OPENSSL_FULLPATH/include" LDFLAGS="-L$GCC_LIBRARY_PATH" ./configure --with-openssl --disable-ldap
LD_LIBRARY_PATH=$GCC_LIBRARY_PATH make
cp lib/.libs/*.so* $GCC_LIBRARY_PATH
make clean
popd

# Build static openssl with musl
pushd $OPENSSL_DIR
make clean
CC=$MUSL_FULLPATH/bin/x86_64-linux-musl-gcc ./Configure linux-x86_64
make
cp *.a $MUSL_LIBRARY_PATH
popd

# Build static libcurl with musl (with static openssl compiled by musl)
pushd $CURL_DIR
LD_LIBRARY_PATH="$MUSL_LIBRARY_PATH:$LD_LIBRARY_PATH" CC=$MUSL_FULLPATH/bin/x86_64-linux-musl-gcc CPPFLAGS="-I$OPENSSL_FULLPATH/include" LDFLAGS="-static -L$MUSL_LIBRARY_PATH" ./configure --with-openssl -disable-shared --enable-static --disable-ldap
make
cp lib/.libs/*.a $MUSL_LIBRARY_PATH
make clean
popd

# Build ecec with gcc (with openssl and libcurl compiled by gcc)
pushd $ECEC_DIR
gcc -Iinclude/ -I$CURL_FULLPATH/include -L$CURL_FULLPATH/lib/.libs/ -I$OPENSSL_FULLPATH/include -L$GCC_LIBRARY_PATH src/*.c -shared -o libecec.so -lcrypto
cp libecec.so $GCC_LIBRARY_PATH

# Build static ecec with musl (with openssl andlibcurl compiled by musl)
mkdir -p build
pushd build
OPENSSL_ROOT_DIR=$OPENSSL_FULLPATH CC=$MUSL_FULLPATH/bin/x86_64-linux-musl-gcc cmake ..
make
cp libece.a $MUSL_LIBRARY_PATH
popd
popd

# Build liburing (with gcc)
pushd liburing-$LIBURING_DIR # idk why they called it liburing-liburing-2.4
./configure
make
cp src/liburing.so.2.4 $GCC_LIBRARY_PATH
pushd $GCC_LIBRARY_PATH
ln -s liburing.so.2.4 liburing.so.2
ln -s liburing.so.2 liburing.so
popd
popd

# Build liburing (with musl)
pushd liburing-$LIBURING_DIR # idk why they called it liburing-liburing-2.4
./configure --cc=$MUSL_FULLPATH/bin/x86_64-linux-musl-gcc
make clean
make
cp src/liburing.a $MUSL_LIBRARY_PATH
popd

touch $DONE_MARKER

popd
