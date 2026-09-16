# Desktop preview and timeline

Milestones 4–5 provide an optional Qt desktop, an isolated playback worker, and indexed
paused seeking/frame stepping with a shared source clock.
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
On Ubuntu, the Qt SDK also needs the OpenGL/EGL and xkbcommon development packages;
the desktop CI job lists these prerequisites. Installing only the runtime libraries can
leave Qt's CMake dependency discovery incomplete. See [Qt Linux requirements](https://doc.qt.io/qt-6.10/linux-requirements.html).
Run from the configured build tree or supply the appropriate Qt runtime environment.
Packaging installers and macOS application bundles are deferred.

NLE_BUILD_DESKTOP is off by default. Playback-plan tests remain available without Qt.
When NLE_MEDIA_INTEGRATION=ON and NLE_BUILD_DESKTOP=ON, CTest also exercises the Qt worker
and desktop actions against the generated media corpus. Tests require ffmpeg, ffprobe and
Python 3, and use an offscreen platform with audible output disabled. Video preview requires
ffprobe at runtime to index decoded frames. Negative-origin preview also requires ffmpeg
next to ffprobe (or both on PATH) for a verified temporary packet-copy source.

## Editing and preview

1. Import media from the toolbar. The probe runs in the background and can be cancelled.
   A successful import adds a logical asset and preserves source facts in the project.
2. Select an asset and choose Append to timeline. The sequence, track and clip creation
   form one undoable transaction. The first sequence is created when needed.
3. Play/pause, Stop, drag the position slider, or click the timeline ruler to seek.
   Click a clip to inspect it. Previous frame / Next frame pauses and visits indexed frame
   boundaries within the current clip. After jumping straight to sequence end, seek into its
   last clip first if that clip has not yet been prepared. Missing/changed paths report errors.
4. Set position, source in and duration in seconds. Decimals and exact fractions such as
   1001/30000 are accepted. Apply, Split and Delete use the core commands. Invalid edits
   preserve the document. Undo/redo restores the same editable timeline.
5. Save/Open use native version 4. Versions 1–3 migrate while preserving their timing semantics.
   Closing or replacing an unsaved document prompts before discarding changes.
   Dirty documents receive recovery checkpoints every 30 seconds; [project recovery](project-recovery.md)
   explains draft restoration, Save As and shared writer ownership. Undo history remains session-local, as it does in the CLI.

Use Probe tool… to choose ffprobe if it is not on PATH. Existing offline assets can be
relinked with the CLI's verified relink command; reopen the saved project afterward.
The sequence selector can inspect existing sequences. The graphical ruler is a compact
whole-sequence view; detailed zooming, scrolling and drag editing are later work.

## Supported preview slice

- One populated track per sequence, with the clip's embedded audio if present. Empty extra
  tracks are permitted. Independent populated tracks remain editable but preview rejects
  them rather than silently omitting or mixing content.
- At most one video and one audio stream per source. Shared sources require known stream starts;
  unknown audio-only starts are permitted. Shared-clock assets retain known positive/negative
  origins and unequal stream starts. Legacy assets retain their old independent-stream
  convention; offset legacy sources require re-import as a new asset for shared-clock preview.
- Cuts and silent/blank gaps. The clock holds while a new clip worker loads, so this is not
  seamless or sample-accurate cut playback. Preview stops at sequence end.
- Rational edit times and indexed paused frame selection. The frame held at a seek position
  is selected by actual decoded timestamps through the next presentation time. The worker
  verifies that selected timestamp; the requested playhead remains exact. Internal Qt seeks
  use milliseconds. Live playback scheduling and frame display still follow the backend.
- Timeline/source positions up to 24 hours and source ranges at least a millisecond in the adapter. The core's
  broader valid ranges still save/edit normally but may not qualify for this preview.

No export, effects, multi-track mixing, color-managed/HDR monitoring, persisted frame indexes,
proxy generation or general timeline mixing is included.

## Failure and cancellation behavior

A separate worker owns decoding/audio. Open/seek invalidates old output and coalesces rapid
requests to the latest position. Cancel/Stop kills the worker asynchronously. Missing sources,
changed size, unsupported source layout, failed workers, malformed/oversized IPC, decoder
errors, load timeout and stalled playback are surfaced without changing the project.

Frame preparation has a 30-second total deadline, 200,000-frame limit and 16 MiB process
output limit. A single cache is keyed by path, size, modification time and source metadata.
Negative origins use a temporary packet-copy Matroska source, with frame/stream verification
and a 512 MiB output cap. It is removed when the cached source is replaced or the transport
is destroyed. This does not alter the original file or persisted project locators.

Worker loading defaults to a 10-second deadline; active playback without progress stops after five
seconds. Transfer is capped at 4 MiB queued/buffered data and 1 MiB per encoded preview image.
Frames are resized to at most 960x540 and image transfers limited to roughly 30 fps; this is
an inspection preview, not a lossless render path. The backend is fixed to FFmpeg with local
file protocols and software decoding for the validated baseline.

The worker uses Qt's audio scheduling. Timing tests measure decoder-delivery observations,
not physical speaker/display delay. See [Milestone 5 evaluation](precision-evaluation.md) and
[ADR 0011](adr/0011-source-origins-and-frame-index.md) for evidence and dependency details.
