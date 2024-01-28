#!/bin/bash

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
TOOLCHAIN_DIR=$SCRIPT_DIR/../toolchain
SRC_DIR=$SCRIPT_DIR/../src/notification
BUILD_DIR=$SCRIPT_DIR/../build
OPENSSL_DIR=$(realpath $TOOLCHAIN_DIR/openssl-1.1.1v)
CURL_DIR=$(realpath $TOOLCHAIN_DIR/curl-8.2.1)
ECEC_DIR=$(realpath $TOOLCHAIN_DIR/ecec-master)
LIBURING_DIR=$(realpath $TOOLCHAIN_DIR/liburing-liburing-2.4)
LIBRARIES=$(realpath $TOOLCHAIN_DIR/musl)
CC=$TOOLCHAIN_DIR/x86_64-linux-musl-native/bin/x86_64-linux-musl-gcc

CFLAGS=" -std=gnu11 -Wall -Wextra -Werror "
EXECUTABLE_NAME=notification-server-release

CFLAGS+=" -g -static "
CFLAGS+=" -Wno-unused-function "
#CFLAGS+=" -fsanitize=address,undefined "
CFLAGS+=" -I$LIBURING_DIR/src/include -I$ECEC_DIR/include -I$OPENSSL_DIR/include -I$CURL_DIR/include -I$LIBRARIES "

LDFLAGS=" -L$LIBRARIES "
LDFLAGS+=" -luring -lcurl -lssl -lcrypto -lece -lpthread "

pushd $SRC_DIR

#cppcheck main.c
$CC main.c $CFLAGS -o $BUILD_DIR/$EXECUTABLE_NAME $LDFLAGS

echo "Built executable $(realpath $BUILD_DIR/$EXECUTABLE_NAME)"

popd
