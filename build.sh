#!/bin/sh
# Builds build/d3d9.dll and runs the portable tests. Needs Zig (0.15.2 is the
# tested version) on the PATH, or its path in ZIG_BIN. On Windows without a
# POSIX shell, use build.ps1 instead.
set -eu
cd "$(dirname "$0")"
ZIG="${ZIG_BIN:-zig}"
WARNINGS="-std=c++17 -Wall -Wextra -Werror"
mkdir -p build

# Parser and rule tests. They run on the build machine, whatever it is.
"$ZIG" c++ $WARNINGS source/config-test.cpp -o build/config-test
./build/config-test package/RaceRoomTriple.ini

# The DLL, always for 64-bit Windows.
"$ZIG" c++ -target x86_64-windows-gnu -shared -O2 $WARNINGS \
    source/proxy.cpp source/version.rc source/exports.def -o build/d3d9.dll -ldxguid -luuid

# The smoke test program. Run it on Windows: build/smoke-test.exe build/d3d9.dll
"$ZIG" c++ -target x86_64-windows-gnu -municode -O2 $WARNINGS \
    source/smoke-test.cpp -o build/smoke-test.exe

echo "Built build/d3d9.dll"
