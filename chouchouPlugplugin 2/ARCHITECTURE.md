# chouchouPlugplugin2 — Architecture

## Product law

**Vision:** Let DAWs without native ARA use ARA-only plugs via chouchou.  
**Shipping today:** Audition **Bridge** = Rec/Stop at the chouchou insert → Save… named WAV → **Open in SpectraLayers…** (pick file) → **Standalone** loads SpectraLayers + binds WAV.  
**Also shipping:** in-DAW round trip (drop / Live Import Track / Rec → SpectraLayers inside chouchou → in-place playback or Render WAV), see below.  
**Not shipping:** reliable Audition-process SpectraLayers UI. Audition clip round trip exists outside the plug-in via `../chochou audition tools` (separate folder) (SpectraLayers standalone, see below).

`EmbeddedARAExperimentMode` is **developer-only** (`CHOUCHOU_ENABLE_EMBEDDED_ARA_EXPERIMENT=1`), not the Audition workflow.

## Capture ↔ SpectraLayers (shipping UX)

**What is recorded:** the signal at **chouchou’s insert point**, not the track’s final output. Effects placed after chouchou are not included; to include an effect, put chouchou after it. There is **no DAW track picker** (a plugin cannot enumerate host tracks) — to record another track, move the chouchou insert to that track in Audition.

The Capture bar in the main editor is the **only** recording entry point (slot rows have no Rec in Bridge mode).

1. **Rec** — allocates the take buffer (message thread, max 180 s) and arms recording.  
2. **Source lock** — the first audible block decides the source for the whole take: insert **Output** if its peak ≥ threshold, otherwise **Input (pre-FX snapshot)** if that is audible. The bar shows `Source: Output (insert return)` / `Input (pre-FX at insert)`.  
3. **Stop** — sets recording off, then takes the capture lock so the last in-flight push has finished. Empty or silent (peak &lt; `1e-5` ≈ −100 dBFS) takes are discarded with `CAPTURE_EMPTY` / `CAPTURE_SILENT`. Otherwise → **ReadyToSave** and the save dialog opens (folder + file name; `.wav` is enforced).  
4. **Save** — WAV written via a temporary file then atomically replaced; `Name.wav.session.json` (`targetPlugin=SpectraLayers`, `captureSource`) must also succeed. Any failure (`CAPTURE_WRITE_FAILED` / `SESSION_WRITE_FAILED`) or a cancelled dialog **keeps the take** — use **Save…** to retry or **Discard**.  
5. **Buffer full** — at 180 s recording stops automatically, the take is kept (ReadyToSave, `CAPTURE_INCOMPLETE` diagnostic).  
6. **Open in SL…** — choose any WAV → Bridge launches `Standalone --wav …` (or `--session` if a sidecar exists); Standalone loads SpectraLayers, binds file, opens UI.  
7. Idle does **not** accumulate audio; the bar still shows live In/Out peaks so you can confirm the host is feeding the insert. While recording, ~1 s of silence at both points shows a red “No signal at the insert” warning.

State machine: `Idle → Recording → ReadyToSave → Idle` (Save succeeded or Discard). Rec is disabled in ReadyToSave, so a take is never silently wiped.

## SpectraLayers round trip inside the DAW (Live / Audition)

Inner ARA plug-ins now load in every host (`allowsInnerAraPluginLoad` returns true). The first ARA slot is the round-trip target; if none exists, importing loads SpectraLayers (VST3, must be scanned) into the first empty slot.

**Getting audio in** (each sets the clip's DAW timeline start = `regionStartSample`, saved in the slot state):

| Way | Start position |
|-----|----------------|
| Drop an audio file on the editor (wav/aif/flac/mp3/m4a/caf/ogg) | DAW cursor at drop time; Option held = 0 s |
| **Import Track** (Live only, needs chouchouLink) | Exact: clip `start_time` minus `start_marker` (warped clips: via warp markers, exact only at the original tempo) |
| Slot **ARA Bind** with no take | DAW cursor when the file is picked |
| **Rec → Stop** or **Auto** (one take: next Play → Stop) | Host sample of the first recorded block |

Non-WAV or non-mappable files are converted to 32-bit float WAV under `<capture dir>/imports/`. After binding, the SpectraLayers UI opens automatically (Audition: blocked by default because of the blank SAM window; Option-click **Open UI** to try).

**Getting the result back:**
- **In place** — the ARA playhead is `host time − regionStartSample`. Blocks inside the clip output SpectraLayers' render; outside the clip, or while the DAW is stopped, the slot is skipped and the track stays dry. Pressing Play in SpectraLayers takes the transport locally until the DAW starts playing again.
- **Render** (slot button, DAW stopped) — background thread holds the wrapper's process lock, drives the inner plug-in offline from 0 to the clip length, writes `<source>_SL.wav` next to the source (or the capture dir) and reveals it. The live path for that slot stays dry while rendering.

**chouchouLink install (Live):** copy `Tools/chouchouLink` to `~/Music/Ableton/User Library/Remote Scripts/`, restart Live, Preferences → Link/Tempo/MIDI → Control Surface → `chouchouLink` (no MIDI ports). It listens on `127.0.0.1:9017`, answers `selected_clip` from Live's main thread (`update_display`). Selection: the detail clip if it is an Arrangement audio clip, else the selected track's Arrangement audio clip under the song position, else its first one. Session clips are not supported. The reply carries `picked` (`detail` / `playhead` / `first`); for a guessed clip chouchou only shows track, clip, file and beat range in the status line and imports on a second Import Track press within 10 s (no `AlertWindow`, which can get stuck in some hosts). `tempo_automated` comes from the song tempo parameter's `automation_state` (the Arrangement tempo envelope itself is not readable through the Live API) and adds a misalignment warning.

**Audition (inside the plug-in):** drop / ARA Bind / Rec / Auto only. Constant tempo assumed everywhere (tempo automation breaks warped-clip alignment).

**Audition (outside the plug-in):** a separate CEP panel that sends the clip to the SpectraLayers 13 standalone app, brings the edit back as a new track automatically, and syncs both transports. It no longer lives in this project: see `../chochou audition tools/README.md` (panel, dev bridge, Audition scripting reference, Adobe samples, tests).

## Modes

| Mode | Inner ARA load | SpectraLayers UI |
|------|----------------|------------------|
| AuditionCaptureMode (any plug-in host) | Allowed | Live: yes; Audition: Option-click only |
| EmbeddedARAExperimentMode | Allowed | Developer flag |
| StandaloneFullARAMode | Allowed | Yes when Ready |

## Outer identity

- chouchou = VST3 **Fx** (`JUCE_PLUGINHOST_ARA` for internal hosting)
- Never `IS_ARA_EFFECT` / Never outer `OnlyARA` / Never chouchou `createARAFactory` as Effect

## Diagnostics

Failures expose **Copy Report** — pasteable `=== chouchou diagnostic ===` block for chat.

**Offline self-tests** (`Tools/RackSelfTest`, `Tools/ARASelfTest`): `build_and_run.sh` builds the CMake targets `chouchouRackSelfTest` (linked against the plug-in's current `chouchouPlugplugin2` shared code) / `chouchouARASelfTest` in `build_cmake` and runs them; `CHOUCHOU_ROOT` overrides the project root. RackSelfTest checks that the hosted chouchouCompressor really compresses (0 dBFS gain at least 3 dB below -40 dBFS gain), that its Makeup parameter reaches the output, and that a bypassed slot has no level-dependent gain.

**Hosted plug-ins with extra buses** (sidechain, mono) go through `processHostedInstance`'s adapt path: the scratch buffer has `kAdaptScratchChannels` (16) channels and the plug-in gets a view of exactly its channel count and the block length. Before this, the 2-channel scratch made every sidechain plug-in (e.g. chouchouCompressor) skip silently, and adapted plug-ins were handed the whole 8192-sample scratch.

## Do not claim

- “Fully hosts ARA inside Audition”
- “Real-time SpectraLayers in Audition”
- Capture Document == Audition Clip
