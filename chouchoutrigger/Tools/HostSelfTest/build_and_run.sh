#!/bin/bash
set -euo pipefail

# Mini DAW host: compiles the JUCE modules with VST3 + AU hosting enabled (the plug-in's own
# static library is built without hosting), then loads the *installed* chouchouTrigger bundles.

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/Tools/HostSelfTest/main.cpp"
BUILD="$ROOT/Builds/MacOSX/build/HostSelfTest"
OUT="$ROOT/Builds/MacOSX/build/Release/chouchouTrigger_HostSelfTest"
JUCE="${HOME}/JUCE/modules"
LIBCODE="$ROOT/JuceLibraryCode"
ARCH="$(uname -m)"

mkdir -p "$BUILD" "$(dirname "$OUT")"

UNITS=(
  include_juce_core.mm include_juce_core_CompilationTime.cpp include_juce_core_zlib.c
  include_juce_events.mm include_juce_data_structures.mm
  include_juce_graphics.mm include_juce_graphics_Harfbuzz.cpp include_juce_graphics_Sheenbidi.c
  include_juce_graphics_libjpg_1.c include_juce_graphics_libjpg_2.c include_juce_graphics_libjpg_3.c
  include_juce_graphics_libpng.c include_juce_graphics_lunasvg.c
  include_juce_gui_basics.mm include_juce_gui_basics_2.cpp include_juce_gui_basics_3.cpp
  include_juce_gui_basics_4.cpp include_juce_gui_basics_5.cpp include_juce_gui_extra.mm
  include_juce_audio_basics.mm include_juce_audio_formats.mm
  include_juce_audio_formats_flac_1.c include_juce_audio_formats_flac_2.c
  include_juce_audio_processors_headless.mm include_juce_audio_processors_headless_ara.cpp
  include_juce_audio_processors_headless_lv2_libs.cpp include_juce_audio_processors.mm
)

DEFS=(
  -DNDEBUG=1
  -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
  -DJUCE_STANDALONE_APPLICATION=1
  -DJUCE_PLUGINHOST_VST3=1
  -DJUCE_PLUGINHOST_AU=1
  -DJUCE_MODAL_LOOPS_PERMITTED=1
  -DJUCE_USE_CURL=0
  -DJUCE_WEB_BROWSER=0
  -DJUCE_STRICT_REFCOUNTEDPOINTER=1
  -DJUCE_MODULE_AVAILABLE_juce_core=1
  -DJUCE_MODULE_AVAILABLE_juce_events=1
  -DJUCE_MODULE_AVAILABLE_juce_data_structures=1
  -DJUCE_MODULE_AVAILABLE_juce_graphics=1
  -DJUCE_MODULE_AVAILABLE_juce_gui_basics=1
  -DJUCE_MODULE_AVAILABLE_juce_gui_extra=1
  -DJUCE_MODULE_AVAILABLE_juce_audio_basics=1
  -DJUCE_MODULE_AVAILABLE_juce_audio_formats=1
  -DJUCE_MODULE_AVAILABLE_juce_audio_processors_headless=1
  -DJUCE_MODULE_AVAILABLE_juce_audio_processors=1
)

INCS=(-I"$JUCE" -I"$JUCE/juce_audio_processors_headless/format_types/VST3_SDK")

compile_unit() {
  local unit="$1"
  local src="$LIBCODE/$unit"
  local obj="$BUILD/${unit%.*}.o"
  if [[ -f "$obj" && "$obj" -nt "$0" ]]; then return 0; fi
  case "$unit" in
    *.c)   clang -O2 -arch "$ARCH" -x c "${DEFS[@]}" "${INCS[@]}" -c "$src" -o "$obj" ;;
    *.mm)  clang++ -std=c++17 -O2 -stdlib=libc++ -arch "$ARCH" -x objective-c++ "${DEFS[@]}" "${INCS[@]}" -c "$src" -o "$obj" ;;
    *.cpp) clang++ -std=c++17 -O2 -stdlib=libc++ -arch "$ARCH" -x c++ "${DEFS[@]}" "${INCS[@]}" -c "$src" -o "$obj" ;;
  esac
}

echo "Compiling JUCE host modules ($ARCH, cached in $BUILD)..."
pids=()
for u in "${UNITS[@]}"; do
  compile_unit "$u" &
  pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done

echo "Building HostSelfTest..."
clang++ -std=c++17 -O2 -stdlib=libc++ -arch "$ARCH" -x objective-c++ -fobjc-arc \
  "${DEFS[@]}" "${INCS[@]}" -c "$SRC" -o "$BUILD/main.o"

clang++ -arch "$ARCH" -stdlib=libc++ "$BUILD"/*.o \
  -framework Accelerate -framework AudioToolbox -framework AudioUnit -framework Cocoa \
  -framework CoreAudio -framework CoreAudioKit -framework CoreMIDI -framework Foundation \
  -framework IOKit -framework QuartzCore -framework Security -framework Metal -weak_framework MetalKit \
  -o "$OUT"

echo "Running $OUT"
"$OUT"
