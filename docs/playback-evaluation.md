# Milestone 4 playback evaluation

Executed 2026-09-13 on Windows x64 with MSVC 19.44, Qt 6.10.3 and its FFmpeg 7.1.3
backend. The external FFmpeg/ffprobe 9.0.1 tools generate and probe the corpus; they are
not the preview decoder. Exact dependency configuration and alternatives are in
[ADR 0010](adr/0010-desktop-playback.md). Software decoding is forced for this baseline.

## Verification scope

- Full CTest suite: 31/31 passed in Debug and Release. After the final VFR and time-bound
  refinements, the four affected tests (including their corpus fixture) passed again in both.
- Fresh default/headless Release build: 26/26 passed without Qt or media integration enabled.
- Windows runtime deployment: the desktop smoke test passed with QT_PLUGIN_PATH cleared,
  using the deployed DLLs, multimedia and offscreen plugins. No installer was produced.
- Warnings are errors; clang-format 19 format-check passed. The desktop screenshot was
  inspected for preview rendering, text visibility, clip selection and layout.
- Linux and macOS Apple Silicon/Intel desktop CI jobs are configured. Their execution and
  platform-specific A/V behavior have not been verified from this Windows workspace.

## Local measurements

Each row is one final corpus execution, not a percentile or a hardware performance promise.
Load waits for readiness and, for video, the first image. Seek restarts the isolated worker
and waits for readiness/image at 0.500 seconds. Cancellation measures Stop until process exit.
Filesystem and dependency caches may already be warm. Units below are milliseconds.

| Build | Source | Load | Seek | Cancel | Max A/V delivery skew |
| --- | --- | ---: | ---: | ---: | ---: |
| Debug | tone.wav | 192 | 205 | 20 | N/A |
| Debug | video.mp4 | 229 | 241 | 18 | N/A |
| Debug | av.mkv | 226 | 227 | 20 | 17.385 |
| Debug | vfr.mkv | 228 | 233 | 19 | N/A |
| Release | tone.wav | 166 | 174 | 19 | N/A |
| Release | video.mp4 | 161 | 165 | 18 | N/A |
| Release | av.mkv | 160 | 175 | 19 | 9.499 |
| Release | vfr.mkv | 155 | 172 | 18 | N/A |

[Raw Debug observations](playback-evaluation-Debug.json) and
[raw Release observations](playback-evaluation-Release.json) preserve the sample counts
and decoded timestamps. A zero skew with zero pairs means not applicable, not perfect sync.

## Seeking, VFR and audio timing

The generated corpus consists of 48 kHz stereo PCM silence, 64x48 MPEG-4 at 30000/1001 fps,
64x48 FFV1/PCM Matroska at 24 fps, and 96x64 FFV1 Matroska with irregular timestamps
(approximately 12 fps followed by 5 fps). It does not represent demanding 4K/HDR workloads.

The 0.500-second video seeks returned frame start/end observations of 0.467133/0.500500
seconds (MP4), 0.500/0.541 (A/V Matroska), and 0.500/0.516 (VFR Matroska). All cover
the requested corpus seek within the test tolerance. Core edit times remain rational;
only the preview boundary floors to milliseconds.

A separate seek to 0.550 seconds in sparse VFR media returned the frame starting at 0.583
seconds, 33 ms ahead. A nominal frame end is not a hold interval extending to the next
presentation timestamp. This adapter delegates selection to Qt and permits either adjacent
frame in that regression test. It does not implement exact held-frame lookup, a complete
frame index or frame stepping. This measured limitation motivates the next roadmap milestone.

A/V tests pair nearby decoded audio/video timestamps after startup and measure the
difference between their delivery-clock offsets. The seven matched pairs in each A/V run
passed the 200 ms threshold. This tests decoder delivery with audible output disabled;
it does not measure speaker latency, display scanout, physical lip sync or long-run drift.
The application enables Qt default-device audio during ordinary preview.

## Failure and editing checks

The worker tests exercise pause stability, 25 rapid seeks with only the latest request
accepted, missing/unsupported originals, decoder failure, a missing executable, a hung
worker, and two clips separated by a silent gap reaching sequence end. Cancellation
must complete within one second in normal tests. A simulated load hang uses an 80 ms
deadline and must terminate within 1.5 seconds. Production load/progress deadlines are
10/5 seconds; OS process scheduling is not a hard real-time guarantee.

The headless plan tests check exact source mapping, half-open clip/gap boundaries,
snapshot isolation, multi-track rejection, source origin requirements, oversized source
positions and sub-millisecond clips. Those preview restrictions do not invalidate the
underlying editable project.

The desktop smoke test imports media asynchronously, appends a sequence/track/clip in one
revision, previews it, splits through the command API, undoes/redoes, rejects an invalid
edit without changing the snapshot, saves/reopens the identical native document and
renders the interface. It invokes application controls internally, not desktop automation.

## Reproduce

Configure with NLE_BUILD_DESKTOP=ON, a Qt prefix, and NLE_MEDIA_INTEGRATION=ON. Supply
ffmpeg, ffprobe and Python on PATH or through their CMake cache paths. Then run:

~~~powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --build build --config Release --target format-check deploy-desktop
~~~

Fresh JSON reports and desktop screenshots are written under the ignored build directory.
The deploy-desktop target applies only to Windows. See [setup and use](desktop-preview.md)
for supported operations and the single-populated-track, source-origin and cut-loading limits.
