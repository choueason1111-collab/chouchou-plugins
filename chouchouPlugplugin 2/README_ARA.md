# chouchouPlugplugin2 — ARA host experiment

Original `chouchouPlugplugin` is unchanged. This copy embeds a **mini ARA host** so plugins like SpectraLayers can load even when the outer host (Adobe Audition) has **no ARA**.

## Judgment: do not build a “VM”

| Idea | Verdict |
|------|---------|
| Nested Audition → chouchou → SpectraLayers | Slow / often dead. SpectraLayers Qt/SAM init fails under Audition (“SAM not initialized”). Open UI blank; Play / Go to start look like no-ops because **SL never becomes Ready**. |
| Full OS VM / sandbox | Worse for ARA. ARA needs an **in-process** DocumentController + companion VST3/AU factory — a VM does not give you that bridge. |
| **Standalone = mini ARA DAW** (this app) | **Fastest path.** Same Celemony TestHost-style graph, no Audition Qt conflict. SpectraLayers UI can reach Ready. |
| Pro Tools (native ARA) | Best for production clip↔SL sync when available. |

**Recommended workflow (file bridge — Audition ↔ Standalone are separate processes)**

```
Audition play → ARA Capture
        ↓  writes WAV
~/Documents/chouchouPlugplugin2_ARA/from_audition_….wav
        ↓  drag / Open in Standalone → SpectraLayers
SpectraLayers edit → File/Export (or Process → Bounce) → out.wav
        ↓
Audition File → Import / drag onto timeline
```

There is **no live link**. Capture freezes what Audition just played; SpectraLayers export is how you bring the result back.

1. In Audition: insert chouchou → play the clip → **ARA Capture** (Finder opens the WAV folder).
2. Click **Standalone** → Scan VST3 → load SpectraLayers → drag that WAV (or ARA Host).
3. Edit in SpectraLayers → **export/bounce a new WAV**.
4. Back in Audition: Import that WAV (replace clip or layer it).

## Giants we stand on

| Source | What we reuse |
|--------|----------------|
| **Celemony ARA SDK TestHost / MiniHost** (`~/SDKs/ARA_SDK/ARA_Examples`) | Document graph order, edit cycles, host interfaces |
| **JUCE AudioPluginHost `ARAPlugin.h`** | VST3/AU companion binding via `ARAHostDocumentController` |
| **Not Reaper** | Reaper’s ARA host internals are not open source |

## Document shape (TestHost-aligned)

```
Document
 └─ MusicalContext
     └─ RegionSequence
         └─ PlaybackRegion
              └─ AudioModification
                   └─ AudioSource   (WAV / live-capture freeze)
```

## What this can / cannot do

- **Can:** Host ARA VST3 inside a slot; Capture Audition audio to WAV; local Play / Go to start on the ARA Host waveform (yellow playhead).
- **Cannot:** True Audition↔SpectraLayers ARA timeline sync (Audition has no ARA). Nested SL under Audition often stays blank — that is an Audition/Qt conflict, not a missing Go-to-start wire.

## Build / open Standalone

```bash
cd build_cmake   # inside this folder
cmake --build . --config Release -j --target chouchouPlugplugin2_Standalone chouchouPlugplugin2_VST3 chouchouPlugplugin2_AU
open "./chouchouPlugplugin2_artefacts/Release/Standalone/chouchouPlugplugin2.app"
```

Requires ARA SDK at `~/SDKs/ARA_SDK`.

See also: [ARCHITECTURE.md](ARCHITECTURE.md) (product law: Audition = Bridge, Standalone = ARA Host).
