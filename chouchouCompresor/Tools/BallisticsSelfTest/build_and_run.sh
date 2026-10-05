#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/Tools/BallisticsSelfTest/main.cpp"
OUT="$ROOT/Builds/MacOSX/build/Release/chouchouCompressor_BallisticsSelfTest"
LIB="$ROOT/Builds/MacOSX/build/Release/libchouchouCompressor.a"
JUCE="${HOME}/JUCE/modules"
JUCER="$ROOT/JuceLibraryCode"

if [[ ! -f "$LIB" ]]; then
  echo "Missing $LIB — build Release Shared Code first."
  exit 1
fi

ARCH="$(uname -m)"
echo "Building BallisticsSelfTest ($ARCH)..."

clang++ -std=c++17 -O2 -stdlib=libc++ \
  -arch "$ARCH" \
  -ObjC++ \
  -fobjc-arc \
  -I"$JUCER" \
  -I"$ROOT/Source" \
  -I"$JUCE" \
  -I"$JUCE/juce_audio_processors_headless/format_types/VST3_SDK" \
  -DNDEBUG=1 \
  -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
  -DJUCE_MODULE_AVAILABLE_juce_audio_basics=1 \
  -DJUCE_MODULE_AVAILABLE_juce_audio_devices=1 \
  -DJUCE_MODULE_AVAILABLE_juce_audio_formats=1 \
  -DJUCE_MODULE_AVAILABLE_juce_audio_plugin_client=1 \
  -DJUCE_MODULE_AVAILABLE_juce_audio_processors=1 \
  -DJUCE_MODULE_AVAILABLE_juce_audio_processors_headless=1 \
  -DJUCE_MODULE_AVAILABLE_juce_audio_utils=1 \
  -DJUCE_MODULE_AVAILABLE_juce_core=1 \
  -DJUCE_MODULE_AVAILABLE_juce_data_structures=1 \
  -DJUCE_MODULE_AVAILABLE_juce_dsp=1 \
  -DJUCE_MODULE_AVAILABLE_juce_events=1 \
  -DJUCE_MODULE_AVAILABLE_juce_graphics=1 \
  -DJUCE_MODULE_AVAILABLE_juce_gui_basics=1 \
  -DJUCE_MODULE_AVAILABLE_juce_gui_extra=1 \
  -DJUCE_STANDALONE_APPLICATION=1 \
  -DJUCE_SHARED_CODE=1 \
  -DJUCE_VST3_CAN_REPLACE_VST2=0 \
  -DJUCE_STRICT_REFCOUNTEDPOINTER=1 \
  -DJUCE_DONT_DECLARE_PROJECTINFO=1 \
  "$SRC" \
  "$LIB" \
  -framework Accelerate \
  -framework AudioToolbox \
  -framework Cocoa \
  -framework CoreAudio \
  -framework CoreAudioKit \
  -framework CoreMIDI \
  -framework DiscRecording \
  -framework Foundation \
  -framework IOKit \
  -framework QuartzCore \
  -framework Security \
  -framework WebKit \
  -framework Metal \
  -weak_framework MetalKit \
  -o "$OUT"

echo "Running $OUT"
"$OUT"
