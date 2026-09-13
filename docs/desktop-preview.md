# Desktop preview and timeline

Milestone 4 adds an optional Qt desktop application and an isolated playback worker.
The core, CLI, native format and command boundary remain independent of the UI.

## Build

Requires a Qt 6.8+ SDK with Core, Gui, Widgets, Network, Concurrent and Multimedia.
Qt 6.10.3 is the locally tested/pinned SDK. Enable it explicitly:

~~~powershell
cmake -S . -B build -DNLE_BUILD_DESKTOP=ON -DCMAKE_PREFIX_PATH="D:/Qt/6.10.3/msvc2022_64"
cmake --build build --config Release
cmake --build build --config Release --target deploy-desktop
.\build\Release\editor-desktop.exe --ffprobe "D:\tools\ffprobe.exe"
~~~

On this development machine the SDK is in build/tools/Qt/6.10.3/msvc2022_64. The launcher
in tools/run-desktop.ps1 locates that SDK and the downloaded probe tool, or accepts explicit
paths:

~~~powershell
.\tools\run-desktop.ps1
.\tools\run-desktop.ps1 -ProjectFile "D:\projects\edit.nle"
~~~

Qt is never downloaded by CMake. deploy-desktop copies Windows runtime dependencies
into the ignored build output for local development; it is not a release packaging/license
compliance workflow.

Linux/macOS use the same CMake option and their Qt prefix, then ./build/editor-desktop.
Run from the configured build tree or supply the appropriate Qt runtime environment.
Packaging installers and macOS application bundles are deferred.

NLE_BUILD_DESKTOP is off by default. Playback-plan tests remain available without Qt.
When NLE_MEDIA_INTEGRATION=ON and NLE_BUILD_DESKTOP=ON, CTest also exercises the Qt worker
and desktop actions against the generated media corpus. Tests require ffmpeg, ffprobe and
Python 3, and use an offscreen platform with audible output disabled.

## Editing and preview

1. Import media from the toolbar. The probe runs in the background and can be cancelled.
   A successful import adds a logical asset and preserves source facts in the project.
2. Select an asset and choose Append to timeline. The sequence, track and clip creation
   form one undoable transaction. The first sequence is created when needed.
3. Play/pause, Stop, drag the position slider, or click the timeline ruler to seek.
   Click a clip to inspect it. Source paths that are missing/changed are reported clearly.
4. Set position, source in and duration in seconds. Decimals and exact fractions such as
   1001/30000 are accepted. Apply, Split and Delete use the core commands. Invalid edits
   preserve the document. Undo/redo restores the same editable timeline.
5. Save/Open use native version 3. Closing or replacing an unsaved document prompts before
   discarding changes. Undo history remains session-local, as it does in the CLI.

Use Probe tool… to choose ffprobe if it is not on PATH. Existing offline assets can be
relinked with the CLI's verified relink command; reopen the saved project afterward.
The sequence selector can inspect existing sequences. The graphical ruler is a compact
whole-sequence view; detailed zooming, scrolling and drag editing are later work.

## Supported preview slice

- One populated track per sequence, with the clip's embedded audio if present. Empty extra
  tracks are permitted. Independent populated tracks remain editable but preview rejects
  them rather than silently omitting or mixing content.
- At most one video and one audio stream per source. Stream starts must be zero/aligned;
  unknown audio-only starts are permitted. Other origin conventions report a preview error.
- Cuts and silent/blank gaps. The clock holds while a new clip worker loads, so this is not
  seamless or sample-accurate cut playback. Preview stops at sequence end.
- Rational edit times; explicit millisecond seek quantization. VFR playback follows actual
  media timestamps without assuming constant frame rate. Between sparse VFR frames the backend
  can select the next frame (33 ms ahead in the measured 0.55 s seek). Exact hold-frame lookup
  is deferred; a frame's nominal end timestamp does not define the next presentation time.
- Timeline/source positions up to 24 hours and source ranges at least a millisecond in the adapter. The core's
  broader valid ranges still save/edit normally but may not qualify for this preview.

No export, effects, multi-track mixing, color-managed/HDR monitoring, complete frame index,
frame stepping, proxy generation or nonzero-origin remapping is included.

## Failure and cancellation behavior

A separate worker owns decoding/audio. Open/seek invalidates old output and coalesces rapid
requests to the latest position. Cancel/Stop kills the worker asynchronously. Missing sources,
changed size, unsupported source layout, failed workers, malformed/oversized IPC, decoder
errors, load timeout and stalled playback are surfaced without changing the project.

Loading defaults to a 10-second deadline; active playback without progress stops after five
seconds. Transfer is capped at 4 MiB queued/buffered data and 1 MiB per encoded preview image.
Frames are resized to at most 960x540 and image transfers limited to roughly 30 fps; this is
an inspection preview, not a lossless render path. The backend is fixed to FFmpeg with local
file protocols and software decoding for the validated baseline.

The worker uses Qt's audio scheduling. Timing tests measure decoder-delivery observations,
not physical speaker/display delay. See [evaluation](playback-evaluation.md) and
[ADR 0010](adr/0010-desktop-playback.md) for evidence and dependency details.
