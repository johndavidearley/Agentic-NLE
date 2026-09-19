# Desktop preview and timeline

Milestone 8 adds a coordinated sequence worker for two video and four audio tracks.
The core, CLI, native format and command boundary remain independent of Qt and FFmpeg.
Windows [production acceptance](multitrack-evaluation.md) passed the fixed ten-minute targets.
New platform CI remains pending; [the prototype gate](multitrack-prototype.md) records backend selection.

## Build

Use the pinned Qt 6.10.3 SDK (Core, Gui, Widgets, Network, Concurrent and Multimedia) and its
FFmpeg 7 runtime. Prepare matching public headers/import libraries explicitly before CMake:

~~~powershell
python tools/prepare_ffmpeg.py "D:/Qt/6.10.3/msvc2022_64" build-ffmpeg
cmake -S . -B build -DNLE_BUILD_DESKTOP=ON -DCMAKE_PREFIX_PATH="D:/Qt/6.10.3/msvc2022_64" -DNLE_FFMPEG_ROOT=build-ffmpeg
cmake --build build --config Release
cmake --build build --config Release --target deploy-desktop
.\tools\run-desktop.ps1
~~~

The helper verifies the upstream source archive hash, extracts headers and uses the installed
Qt runtime. It does not build or bundle third-party source. Windows requires MSVC x64 tools;
Linux/macOS create development symlinks to the matching SDK libraries. See ADR 0014 for
licensing and ABI requirements. CMake does not download Qt or FFmpeg.

On this machine Qt is in `build/tools/Qt/6.10.3/msvc2022_64`; the launcher locates it and
ffprobe automatically. Pass `-ProjectFile`, `-QtDirectory` or `-Ffprobe` to override.
Windows deployment copies runtime dependencies into ignored build output, not an installer.
Linux/macOS run `./build/editor-desktop` with their Qt prefix. Ubuntu also needs the packages
listed in CI, plus the Qt `icu` and `qtdeclarative` archives used by the retained comparison worker.

The default core and optional MCP builds require neither Qt nor FFmpeg. Enable
`NLE_MEDIA_INTEGRATION=ON` with the desktop to run generated media, multi-track and supervision
tests. Tests require Python, ffmpeg and ffprobe. The primary benchmark uses silent output;
a separate device-clock test sends silence when a compatible output device is available.

## Editing and preview

1. Import media, then select an asset. **Append to timeline** keeps the existing simple workflow.
   **Place on track...** chooses an existing track or creates a top video/audio track, with an
   exact position, source in and duration. Video placement includes embedded audio by default.
   Audio-only placement is deliberate and creates a separate clip; existing routing is unchanged.
2. Select a clip and use **Track / routing...** to enable/mute the track, set linear gain, or
   choose/disable individual source streams. Each accepted dialog commits one undoable edit.
3. Play, pause, stop, scrub or click the ruler. Previous/next frame now visits the sequence's
   output frame grid, which stays meaningful across different source frame rates and gaps.
   Paused seeking still selects the exact source frame held at the requested timeline position.
4. Trim, split and delete through the inspector. Existing decimal/fraction inputs remain.
   Track-placement inputs use whole seconds or exact fractions. Invalid edits leave the document
   unchanged; undo/redo and MCP use the same typed commands.
5. Save/Open writes native version 5 and migrates versions 1–4 without moving clips, dropping
   embedded audio or replacing their frame rate. [Recovery](project-recovery.md) remains active.

Sequence output settings are persisted and editable through the typed command/MCP interface.
A broader output-settings UI, zoom, snapping, waveforms and drag editing belong to Milestone 9.

## Playback contract

- Stored track order is top to bottom. The first enabled active video wins; uncovered output
  is black. Audio from hidden video tracks continues unless muted or routed off.
- Stereo float at 48 kHz: sum in track order, multiply by linear gain (1000 = unity), then
  hard-clip to [-1,1]. Muting affects audio; disabling affects both audio and video.
- Output samples are on the global 48 kHz grid. A fractional clip boundary starts/ends at the
  first output sample on/after that boundary. Source sampling floors the mapped source time;
  FFmpeg resamples/downmixes other supported source rates/layouts. Coarse packet timestamps
  within one timestamp tick preserve sample continuity. Larger timestamp gaps remain silent.
- Source coordinates retain the persisted shared origin, including negative starts and delayed
  streams. Legacy per-stream origins stay independent. Explicit routing selects recorded stream
  indices; automatic routing selects the first stored matching stream.
- Video is evaluated at exact output frame times. Hold the last decoded presentation timestamp
  at/before the mapped position until the next frame or declared stream end. Preview letterboxes
  into at most 960x540, respecting sample aspect ratio and the source color matrix/range
  (unspecified matrix uses FFmpeg's default). Images cross IPC as JPEG previews.
- One clock: the audio sink's processed samples when audible, or monotonic time when silent.
  A bounded queue preloads 240 ms and retains at most 320 ms, with a 64 MiB hard limit.
  The producer encodes preview images before enqueueing; image encoding never blocks the clock.
  Cuts decode ahead without restarting the worker. Seek/edit/stop discards the previous process.
- Initial measured profile: 1920x1080 SDR sources at 30 or 30000/1001 fps, VFR included;
  two video/four audio tracks, speed 1, hard cuts. Output permits 1..60 fps, up to 24 hours;
  timing denominators above one billion are outside preview support. Other performance profiles
  are unmeasured. Rotated and HDR inputs require normalization; no color-managed display claim.
- Audio seeking decodes from the source origin to retain a stable sample/resampler anchor.
  Long sources may hit the five-second operation deadline; no unrestricted long-source seek
  performance claim. The project remains editable when preview rejects a source/profile.

No export, effects, transparency, transitions, proxy generation or physical speaker/display
synchronization claim is included. Old single-track Qt playback remains for regression and
prototype comparison only.

## Failure and cancellation behavior

A separate worker owns decoding/audio. Open/seek invalidates old output and coalesces rapid
requests to the latest position. Cancel/Stop kills the worker asynchronously. Missing sources,
changed size, unsupported source layout, failed workers, malformed/oversized IPC, decoder
errors, load timeout and stalled playback are surfaced without changing the project.
