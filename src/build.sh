#!/bin/bash

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

CFLAGS="-std=gnu11 -Wall -Wextra -Wbad-function-cast -Wcast-align -Wfloat-equal -Wlogical-op -Wmissing-include-dirs -Wnested-externs -Wredundant-decls -Wsequence-point -Wshadow -Wstrict-prototypes -Wswitch -Wundef -Wunreachable-code -Wunused-but-set-parameter -Wwrite-strings -Wpointer-arith"

BUILD_DIR=../build
mkdir -p $SCRIPT_DIR/$BUILD_DIR

if [ "$1" == "ws-debug" ]; then
	SRC_DIR=websocket
	EXECUTABLE_NAME=ws-server-debug

	CFLAGS+=" -O0 -g"
	CFLAGS+=" -Wno-unused-function -Wno-discarded-qualifiers"
	CFLAGS+=" -fsanitize=address,undefined "

	LDFLAGS=" -luring -lcrypt"

	pushd $SCRIPT_DIR > /dev/null

	echo "Checking WEBSOCKET DEBUG"
	cppcheck $SRC_DIR/main.c

	echo "Building WEBSOCKET DEBUG"
	gcc $SRC_DIR/main.c $CFLAGS -o $SCRIPT_DIR/$BUILD_DIR/$EXECUTABLE_NAME $LDFLAGS
    echo "Built executable $(realpath $SCRIPT_DIR/$BUILD_DIR/$EXECUTABLE_NAME)"

	popd >/dev/null
elif [ "$1" == "ws-release" ]; then
	SRC_DIR=websocket
	EXECUTABLE_NAME=ws-server-release
	BUILD_DIR=../build
	LOCAL_BUILD_DIR=docker-build

	CFLAGS+=" -Os -static -g"
	CFLAGS+=" -Wno-unused-function "
	LDFLAGS=" -luring -lcrypt"

	pushd $SCRIPT_DIR >/dev/null

	mkdir -p $LOCAL_BUILD_DIR

	echo "Building WEBSOCKET RELEASE (docker)"
	docker run --rm \
		--user "$(id -u):$(id -g)" \
		-v $(pwd):/opt/src \
		-v $(pwd)/$LOCAL_BUILD_DIR:/opt/build \
		chat-musl-build \
		gcc $CFLAGS src/$SRC_DIR/main.c -o build/$EXECUTABLE_NAME $LDFLAGS

	mv $LOCAL_BUILD_DIR/$EXECUTABLE_NAME $SCRIPT_DIR/$BUILD_DIR
	echo "Built executable $(realpath $SCRIPT_DIR/$BUILD_DIR/$EXECUTABLE_NAME)"
	rm -r $LOCAL_BUILD_DIR

	popd >/dev/null
elif [ "$1" == "media-debug" ]; then
	SRC_DIR=media
	EXECUTABLE_NAME=media-server-debug

	CFLAGS+=" -O0 -g"
	CFLAGS+=" -Wno-unused-function -Wno-discarded-qualifiers -Wno-bad-function-cast -Wno-float-equal"
	# CFLAGS+=" -fsanitize=address,undefined "

	LDFLAGS=" -luring -lm"

	pushd $SCRIPT_DIR > /dev/null

	echo "Building MEDIA DEBUG"
	gcc $SRC_DIR/main.c $CFLAGS -o $SCRIPT_DIR/$BUILD_DIR/$EXECUTABLE_NAME $LDFLAGS
    echo "Built executable $(realpath $SCRIPT_DIR/$BUILD_DIR/$EXECUTABLE_NAME)"

	popd >/dev/null
elif [ "$1" == "media-release" ]; then
	SRC_DIR=media
	EXECUTABLE_NAME=media-server-release
	BUILD_DIR=../build
	LOCAL_BUILD_DIR=docker-build

	CFLAGS+=" -Os -static"
	CFLAGS+=" -Wno-unused-function -Wno-bad-function-cast -Wno-float-equal"
	LDFLAGS=" -luring"

	pushd $SCRIPT_DIR >/dev/null

	mkdir -p $LOCAL_BUILD_DIR

	echo "Building MEDIA RELEASE (docker)"
	docker run --rm \
		--user "$(id -u):$(id -g)" \
		-v $(pwd):/opt/src \
		-v $(pwd)/$LOCAL_BUILD_DIR:/opt/build \
		chat-musl-build \
		gcc $CFLAGS src/$SRC_DIR/main.c -o build/$EXECUTABLE_NAME $LDFLAGS

	mv $LOCAL_BUILD_DIR/$EXECUTABLE_NAME $SCRIPT_DIR/$BUILD_DIR
	echo "Built executable $(realpath $SCRIPT_DIR/$BUILD_DIR/$EXECUTABLE_NAME)"
	rm -r $LOCAL_BUILD_DIR

	popd >/dev/null
elif [ "$1" = "ws-tests" ]; then
	SRC_DIR=tests
	EXECUTABLE_NAME=ws-tests
	BUILD_DIR=../build

	CFLAGS+=" -O0 -g"
	CFLAGS+=" -Wno-unused-function -Wno-discarded-qualifiers"
	CFLAGS+=" -fsanitize=address,undefined "

	LDFLAGS=" -luring -lcrypt"

	pushd $SCRIPT_DIR > /dev/null

	echo "Building TESTS"
	gcc $SRC_DIR/unit.c $CFLAGS -o $SCRIPT_DIR/$BUILD_DIR/$EXECUTABLE_NAME $LDFLAGS
	echo "Built executable $(realpath $SCRIPT_DIR/$BUILD_DIR/$EXECUTABLE_NAME)"

	popd >/dev/null
else
	echo "No target specified, no work to do. No money.. No food.. Death."
fi
