#!/bin/sh
set -eu

PROJECT_DIR=$(readlink -f "$(dirname "$(readlink -f "$0")")"/..)
TOOLCHAIN_DIR="$PROJECT_DIR"/toolchain
BUILD_DIR="$PROJECT_DIR"/build
CODE_DIR=$PROJECT_DIR/src/websocket
Compiler="$TOOLCHAIN_DIR"/x86_64-linux-musl-native/bin/x86_64-linux-musl-gcc

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

WarningFlags="-Wall -Wextra -Werror -Wno-unused-function"
CompilerFlags="-g -static -std=gnu11 -I$BUILD_DIR/liburing/src/include $WarningFlags"
LinkerFlags="-L$TOOLCHAIN_DIR/musl-libs -luring -lcrypt"

$Compiler $CompilerFlags "$CODE_DIR"/main.c -o ws-server-release $LinkerFlags
