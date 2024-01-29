#!/bin/sh
set -eu

PROJECT_DIR=$(readlink -f "$(dirname "$(readlink -f "$0")")"/..)
TOOLCHAIN_DIR="$PROJECT_DIR"/toolchain
BUILD_DIR="$PROJECT_DIR"/build
CODE_DIR=$PROJECT_DIR/src/notification
Compiler="$TOOLCHAIN_DIR"/x86_64-linux-musl-native/bin/x86_64-linux-musl-gcc

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

WarningFlags="-Wall -Wextra -Werror -Wno-unused-function"
CompilerFlags="-g -static -pthread -std=gnu11 -I$BUILD_DIR/liburing/src/include -I$BUILD_DIR/openssl/include -I$BUILD_DIR/curl/include -I$BUILD_DIR/ecec/include $WarningFlags"
LinkerFlags="-L$TOOLCHAIN_DIR/musl-libs -luring -lcurl -lssl -lcrypto -lece"

$Compiler $CompilerFlags "$CODE_DIR"/main.c -o notification-server-release $LinkerFlags
