#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
"${ZIG_BIN:-zig}" c++ -target x86_64-windows-gnu -shared -O2 -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-cast-function-type-mismatch source/proxy.cpp source/exports.def -o build/d3d9.dll -ldxguid -luuid
