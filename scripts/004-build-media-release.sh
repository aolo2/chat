#!/bin/bash

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
TOOLCHAIN_DIR=$SCRIPT_DIR/../toolchain
SRC_DIR=$SCRIPT_DIR/../src/media
BUILD_DIR=$SCRIPT_DIR/../build
LIBURING_DIR=$(realpath $TOOLCHAIN_DIR/liburing-liburing-2.4)
CC=$TOOLCHAIN_DIR/x86_64-linux-musl-native/bin/x86_64-linux-musl-gcc


CFLAGS=" -std=gnu11 -Wall -Wextra -Werror "
EXECUTABLE_NAME=media-server-release

CFLAGS+=" -g -static "
CFLAGS+=" -Wno-unused-function -Wno-discarded-qualifiers -Wno-bad-function-cast -Wno-float-equal"
CFLAGS+=" -I$LIBURING_DIR/src/include "

LDFLAGS=" -L$LIBURING_DIR/src "
LDFLAGS+=" -luring -lm "

mkdir -p $BUILD_DIR
pushd $SRC_DIR

# cppcheck main.c
$CC main.c $CFLAGS -o $BUILD_DIR/$EXECUTABLE_NAME $LDFLAGS
echo "Built executable $(realpath $BUILD_DIR/$EXECUTABLE_NAME)"

popd
