#!/bin/bash
# Set CHOUCHOU_ROOT when this script is not inside the project's Tools/ARASelfTest folder.
set -euo pipefail

ROOT="${CHOUCHOU_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}"
if [[ ! -f "$ROOT/Source/PluginProcessor.h" || ! -f "$ROOT/CMakeLists.txt" ]]; then
  echo "Project root not found at '$ROOT' (needs Source/PluginProcessor.h and CMakeLists.txt)." >&2
  echo "Run this from Tools/ARASelfTest inside the project, or set CHOUCHOU_ROOT=/path/to/project." >&2
  exit 1
fi

BUILD="$ROOT/build_cmake"
cmake -S "$ROOT" -B "$BUILD" >/dev/null
cmake --build "$BUILD" --target chouchouARASelfTest --config Release -j"$(sysctl -n hw.ncpu)"

BIN="$BUILD/chouchouARASelfTest_artefacts/Release/chouchouARASelfTest"
if [[ ! -x "$BIN" ]]; then
  BIN="$BUILD/chouchouARASelfTest_artefacts/chouchouARASelfTest"
fi
if [[ ! -x "$BIN" ]]; then
  echo "Build finished but chouchouARASelfTest was not found under $BUILD." >&2
  exit 1
fi

echo "Running $BIN"
exec "$BIN"
