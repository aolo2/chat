#!/bin/bash

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
LD_LIBRARY_PATH="$(realpath $SCRIPT_DIR/../toolchain/gcc):$LD_LIBRARY_PATH" $SCRIPT_DIR/../build/notification-server-debug "$@"
