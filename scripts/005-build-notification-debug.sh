#!/bin/sh
set -eu

PROJECT_DIR=$(readlink -f "$(dirname "$(readlink -f "$0")")"/..)
TOOLCHAIN_DIR="$PROJECT_DIR"/toolchain
BUILD_DIR="$PROJECT_DIR"/build
CODE_DIR=$PROJECT_DIR/src/notification

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

WarningFlags="-Wall -Wextra -Werror -Wno-unused-function"
CompilerFlags="-O0 -g -std=gnu11 -pthread -I$BUILD_DIR/liburing/src/include -I$BUILD_DIR/openssl/include -I$BUILD_DIR/curl/include -I$BUILD_DIR/ecec/include $WarningFlags"
LinkerFlags="-L$TOOLCHAIN_DIR/native-libs -lcurl -lcrypto -lssl -lecec -luring"

gcc $CompilerFlags "$CODE_DIR"/main.c -o notification-server-debug $LinkerFlags
