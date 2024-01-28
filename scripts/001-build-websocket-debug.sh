#!/bin/bash

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
TOOLCHAIN_DIR=$SCRIPT_DIR/../toolchain
SRC_DIR=$SCRIPT_DIR/../src/websocket
BUILD_DIR=$SCRIPT_DIR/../build
LIBURING_DIR=$(realpath $TOOLCHAIN_DIR/liburing-liburing-2.4)

CFLAGS=" -std=gnu11 -Wall -Wextra -Werror "
EXECUTABLE_NAME=ws-server-debug

CFLAGS+=" -O0 -g "
CFLAGS+=" -Wno-unused-function "
CFLAGS+=" -I$LIBURING_DIR/src/include "
#CFLAGS+=" -fsanitize=address,undefined "

LDFLAGS=" -L$LIBURING_DIR/src "
LDFLAGS+=" -luring -lcrypt "

pushd $SRC_DIR

# cppcheck main.c
gcc main.c $CFLAGS -o $BUILD_DIR/$EXECUTABLE_NAME $LDFLAGS
echo "Built executable $(realpath $BUILD_DIR/$EXECUTABLE_NAME)"

popd
