# Milestone 5 precision evaluation

Executed 2026-09-14 on Windows x64, MSVC 19.44, Qt 6.10.3 and Qt's FFmpeg 7.1.3 playback
backend. External FFmpeg/ffprobe 9.0.1 generate fixtures, probe/index media and normalize
negative packet origins. Software decoding is forced; audible output is disabled in tests.
[ADR 0011](adr/0011-source-origins-and-frame-index.md) records the timing contract and choice.

## Verification

The complete CTest suite passed 33/33 in Debug (66.33 seconds) and Release (46.22 seconds),
with warnings treated as errors. This includes the unchanged editing/session invariants,
malformed native files, source probing/commands, supervised processes and desktop behavior.
The desktop screenshot was inspected for visible frame controls, preview and inspector layout.
The separately configured default/headless Release build passed 27/27 without Qt or external
media integration. clang-format 19.1.5 format-check passed. The deployed Windows runtime
passed the desktop smoke test with QT_PLUGIN_PATH cleared, using its local multimedia and
offscreen plugins. Translation catalogs are not included in this English development build;
no installer or distribution package was produced.

The new source-timing suite checks signed rational arithmetic/overflow, literal version-3
migration, version-4 persistence, relink clock retention and rejection, exact VFR lookup and
bounded stepping. The real negative-origin source also passed a CLI import/save/reload check:
its signed container origin remains -1 second in native version 4.

The precision suite checks shared positive/negative origins, unequal A/V starts, a blank
video lead-in, paused seeks, forward/backward stepping, backward stepping from sequence end,
rejection of a frame cache belonging to another clip, cancelled preparation and longer A/V
scheduling. Desktop frame buttons leave the project snapshot unchanged. Paused playhead
positions remain exactly fixed, including while obsolete worker output is retired.

## Frame selection

Each fixture is sought at exact source times 0.550, 0.913 and 1.370 seconds. Each seek waits
for a decoded image whose timestamp matches the independent original-source index, while
the requested playhead stays unchanged. Six forward/backward steps and one step back from
sequence end are checked per fixture. Timestamp error units below are microseconds.

| Source | Indexed frames | Debug max PTS error | Release max PTS error |
| --- | ---: | ---: | ---: |
| vfr.mkv | 17 | 0 | 0 |
| bframes.mp4 | 120 | 1 | 1 |
| offset-common.mkv | 96 | 0 | 0 |
| offset-audio.mkv | 96 | 0 | 0 |
| offset-video.mkv | 84 | 0 | 0 |
| negative-origin.mkv | 96 | 0 | 0 |

The VFR source holds its 0.500-second frame at a 0.550-second seek. The old direct Qt seek
selected the later 0.583-second frame. The index now defines the held interval through the
next presentation timestamp instead of relying on nominal frame duration or average fps.
The MPEG-4 B-frame fixture uses 30000/1001 fps; its one-microsecond discrepancy is integer
rescaling. The worker accepts at most two microseconds of PTS rescaling discrepancy.

The positive-offset fixtures begin at +2 seconds. In offset-audio, audio starts 0.5 seconds
after video; in offset-video, video starts 0.5 seconds after audio and initial preview is blank.
For negative-origin.mkv, audio starts at -1 second and video at -0.984 seconds: the common
clock retains the 16-millisecond video delay. A verified temporary packet-copy source allows
Qt to seek those early packets. Every original/prepared video timestamp, held interval and
frame count must agree, with stream offsets exact and estimated durations within 1 ms.

The audio-only negative-origin fixture is also normalized. After a 0.250-second seek, the
first observed decoded audio buffer starts at 0.235 seconds in both builds. The buffer overlaps
the requested time; this is not a claim of sample-accurate audio trimming.

## Longer A/V observations

A 12-second, 24-fps FFV1/48-kHz PCM fixture starts at +2 seconds with audio delayed by 0.5
seconds. Playback is observed through source time 11 seconds. Both builds report first video
PTS 0 and first audio PTS 500,000 microseconds, preserving the intended relative start.

For every audio buffer, pair the closest observed video timestamp within 50 ms. Delivery
skew is the wall-clock delivery difference minus the media timestamp difference. Drift is
the absolute difference between mean skew at 1-2 seconds and at 10 seconds onward. These
are decoder-delivery observations; they do not measure speakers, display scanout or physical
lip sync. The limits are 200 ms maximum delivery skew and 100 ms change in mean offset.

| Build | Max delivery skew (ms) | Mean offset drift (ms) | Early / late pairs | Preparation cancel (ms) |
| --- | ---: | ---: | ---: | ---: |
| Debug | 43.968 | 0.062 | 47 / 49 | 12 |
| Release | 13.259 | 0.107 | 47 / 49 | 12 |

Each row is one local execution, not a percentile or a hardware guarantee. Caches may be
warm; scheduler load affects timing. Preparation cancellation measures cancellation of a
queued/running background index request using a deliberately nonresponsive probe executable.
The underlying process suite separately covers cancellation before and during execution.

[Raw Debug observations](precision-evaluation-Debug.json) and
[raw Release observations](precision-evaluation-Release.json) preserve these measurements.
The [Milestone 4 evaluation](playback-evaluation.md) remains a historical baseline.

## Reproduce and boundaries

~~~sh
cmake -S . -B build -DNLE_BUILD_DESKTOP=ON -DNLE_MEDIA_INTEGRATION=ON -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
~~~

Supply NLE_FFPROBE_EXECUTABLE, NLE_FFMPEG_EXECUTABLE and Python3_EXECUTABLE if the tools are
not on PATH. The generated corpus lives in build/media-corpus; reports and screenshots stay
in the build directory. The tests require Qt's multimedia and offscreen plugins.

The earlier [baseline CI run](https://github.com/johndavidearley/Agentic-NLE/actions/runs/34760353314)
reported missing OpenGL development dependencies on Linux and pause-position drift on Intel
macOS. This change adds Linux development packages and discards paused worker-position
updates. Updated Windows/Linux/macOS Apple Silicon/Intel jobs also retain precision reports.
Execution of those jobs for Milestone 5 remains pending; only Windows was run locally.

Preview still supports one populated track and one video/embedded audio stream per source.
Frame preparation is limited to 200,000 frames, 16 MiB process output and a 30-second total
deadline. Negative-origin packet copies have a 512 MiB output cap (up to 1 MiB packet overrun)
and are deleted with the cache. Unsupported or unverifiable sources report errors. No
persistent proxies, independent track mixing, seamless cuts, export, color-managed monitoring
or sample-accurate audio boundaries are promised. [Desktop use](desktop-preview.md) describes
the controls and supported range.
