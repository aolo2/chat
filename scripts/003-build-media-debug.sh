#!/bin/sh
set -eu

PROJECT_DIR=$(readlink -f "$(dirname "$(readlink -f "$0")")"/..)
TOOLCHAIN_DIR="$PROJECT_DIR"/toolchain
BUILD_DIR="$PROJECT_DIR"/build
CODE_DIR=$PROJECT_DIR/src/media

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

WarningFlags="-Wall -Wextra -Werror -Wno-unused-function -Wno-discarded-qualifiers -Wno-bad-function-cast -Wno-float-equal"
CompilerFlags="-O0 -g -std=gnu11 -I$BUILD_DIR/liburing/src/include $WarningFlags"
LinkerFlags="-L$TOOLCHAIN_DIR/native-libs -luring -lm"

gcc $CompilerFlags "$CODE_DIR"/main.c -o media-server-debug $LinkerFlags
