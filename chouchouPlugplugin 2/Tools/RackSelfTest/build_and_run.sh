#!/bin/bash
# Builds Tools/RackSelfTest against the plug-in's current CMake shared code and runs it.
# Set CHOUCHOU_ROOT when this script is not inside the project's Tools/RackSelfTest folder.
set -euo pipefail

ROOT="${CHOUCHOU_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}"
if [[ ! -f "$ROOT/Source/PluginProcessor.h" || ! -f "$ROOT/CMakeLists.txt" ]]; then
  echo "Project root not found at '$ROOT' (needs Source/PluginProcessor.h and CMakeLists.txt)." >&2
  echo "Run this from Tools/RackSelfTest inside the project, or set CHOUCHOU_ROOT=/path/to/project." >&2
  exit 1
fi

BUILD="$ROOT/build_cmake"
cmake -S "$ROOT" -B "$BUILD" >/dev/null
cmake --build "$BUILD" --target chouchouRackSelfTest --config Release -j"$(sysctl -n hw.ncpu)"

BIN="$BUILD/Release/chouchouRackSelfTest"
[[ -x "$BIN" ]] || BIN="$BUILD/chouchouRackSelfTest"
if [[ ! -x "$BIN" ]]; then
  echo "Build finished but chouchouRackSelfTest was not found under $BUILD." >&2
  exit 1
fi

echo "Running $BIN"
exec "$BIN"
