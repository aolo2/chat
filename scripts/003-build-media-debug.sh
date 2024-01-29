#!/bin/bash

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
TOOLCHAIN_DIR=$SCRIPT_DIR/../toolchain
SRC_DIR=$SCRIPT_DIR/../src/media
BUILD_DIR=$SCRIPT_DIR/../build
LIBURING_DIR=$(realpath $TOOLCHAIN_DIR/liburing-liburing-2.4)

CFLAGS=" -std=gnu11 -Wall -Wextra -Werror "
EXECUTABLE_NAME=media-server-debug

CFLAGS+=" -O0 -g "
CFLAGS+=" -Wno-unused-function -Wno-discarded-qualifiers -Wno-bad-function-cast -Wno-float-equal "
CFLAGS+=" -I$LIBURING_DIR/src/include "
#CFLAGS+=" -fsanitize=address,undefined "

LDFLAGS=" -L$LIBURING_DIR/src "
LDFLAGS+=" -luring -lm "

mkdir -p $BUILD_DIR
pushd $SRC_DIR

# cppcheck main.c
gcc main.c $CFLAGS -o $BUILD_DIR/$EXECUTABLE_NAME $LDFLAGS
echo "Built executable $(realpath $BUILD_DIR/$EXECUTABLE_NAME)"
popd
