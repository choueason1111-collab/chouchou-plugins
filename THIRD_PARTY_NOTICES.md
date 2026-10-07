# Third-party notices

The chouchou plugins are licensed under the GNU Affero General Public License v3.0
(see `LICENSE`). They are built with the following third-party software.

| Component | Used for | Licence |
|---|---|---|
| [JUCE](https://github.com/juce-framework/JUCE) | Plugin framework (all plugins) | AGPLv3 (or the commercial JUCE licence) |
| [VST3 SDK](https://github.com/steinbergmedia/vst3sdk) (bundled with JUCE) | VST3 format | MIT |
| [AudioUnitSDK](https://github.com/apple/AudioUnitSDK) (bundled with JUCE) | AU format (macOS) | Apache 2.0 |
| [ARA SDK](https://github.com/Celemony/ARA_SDK) | ARA hosting in chouchouPlugplugin 2 | Apache 2.0 |
| FLAC, Ogg Vorbis, zlib, libpng, jpeglib, HarfBuzz, SheenBidi, LunaSVG, PlutoVG (bundled with JUCE) | Audio file, image, font and SVG support | BSD / zlib / IJG / MIT / Apache 2.0 |

The full list of JUCE's dependencies and their licences is in JUCE's
[LICENSE.md](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md).

`chouchouPlugplugin 2/Source/ARAPluginHostBase.h` is taken from JUCE and keeps its
original copyright notice (Raw Material Software Limited, AGPLv3).

VST is a trademark of Steinberg Media Technologies GmbH. Audio Units is a trademark of
Apple Inc. ARA is a trademark of Celemony Software GmbH. Other product names mentioned
in the source code belong to their respective owners and are used only to identify
compatible hosts and plugins.
