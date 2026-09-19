# ADR 0014: Multi-track decoding backend evaluation

Status: accepted backend selection after the prototype gate, 2026-09-16.
Windows [production acceptance](../multitrack-evaluation.md) passed on 2026-09-18.
New Linux/macOS platform validation remains pending.

The required comparison is defined in [the prototype record](../multitrack-prototype.md).
The optional NLE_MULTITRACK_PROTOTYPE build compares six instances of the current supervised
Qt player with a coordinator receiving decoded frames and PCM from direct FFmpeg APIs.
The comparison results precede production integration and the version 5 project migration.

The direct candidate dynamically links libavformat, libavcodec, libavutil, libswscale and
libswresample. The initial Windows experiment uses the Qt-provided FFmpeg 7.1.3 runtime
already inventoried in ADR 0010, with upstream public 7.1.3 headers. No third-party source
or binary is committed. CMake requires explicit development files and does not fetch them.

FFmpeg is LGPL-2.1-or-later with build-dependent GPL options; the selected Qt runtime has
no GPL/nonfree configure flags (see ADR 0010). An installer would need matching source,
configuration, notices and replacement/relinking rights. The external FFmpeg 9.0.1 GPL
fixture generator remains separate. This evaluation does not authorize a packaged release.
[Upstream licensing](https://ffmpeg.org/legal.html).

Alternatives: keep Qt players and implement a coordinated raw-PCM protocol; use direct
FFmpeg with bounded queues and an application-owned clock; evaluate GStreamer if neither
candidate meets the fixed profile. GStreamer is not being claimed as measured here.
The final decision must follow the recorded measurements, without weakening thresholds.

## Decision and integration contract

Select direct FFmpeg decoding: its ten-minute prototype passed the fixed targets, while the
existing six Qt players diverged by 880 ms and have no shared sample mixer. Keep Qt Widgets
and a supervised local process. The sequence worker owns FFmpeg, bounded predecoded chunks
and a single stereo QAudioSink. Video follows that sink's processed-sample clock. Tests with
output disabled use one monotonic clock; neither path claims physical device synchronization.
Opening, seeking, pausing or editing retires the old process and discards its socket before
starting the latest request. The old single-track implementation remains only for regression
and benchmark comparisons. The editor and MCP operate on the same backend-neutral settings.

Only FFmpeg 7 ABI libraries (avformat/avcodec 61, avutil 59, swscale 8, swresample 5) are used.
The optional desktop build now requires matching development headers and import libraries.
The dependency-free core and Qt-free MCP builds remain available. Packagers must retain the
selected library configuration and applicable source/notices; this milestone does not publish
an installer. Windows measurements do not establish Linux/macOS performance or a platform pass.
