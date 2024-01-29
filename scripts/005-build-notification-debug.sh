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
LIBRARIES=$(realpath $TOOLCHAIN_DIR/gcc)

CFLAGS=" -std=gnu11 -Wall -Wextra -Werror "
EXECUTABLE_NAME=notification-server-debug

CFLAGS+=" -O0 -g "
CFLAGS+=" -Wno-unused-function "
#CFLAGS+=" -fsanitize=address,undefined "
CFLAGS+=" -I$ECEC_DIR/include -I$OPENSSL_DIR/include -I$CURL_DIR/include -I$LIBURING_DIR/src/include "

LDFLAGS=" -L$LIBRARIES "
LDFLAGS+=" -lcurl -lcrypto -lssl -lecec -luring -lpthread "

mkdir -p $BUILD_DIR
pushd $SRC_DIR

# cppcheck main.c
gcc main.c $CFLAGS -o $BUILD_DIR/$EXECUTABLE_NAME $LDFLAGS
echo "Built executable $(realpath $BUILD_DIR/$EXECUTABLE_NAME)"

popd
