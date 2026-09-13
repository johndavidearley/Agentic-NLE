# ADR 0010: Qt Widgets and an isolated Qt Multimedia preview worker

Status: accepted for the bounded Milestone 4 preview, finalized 2026-09-13.
Extends ADRs 0001, 0005 and 0009. The headless core has no Qt dependency.

## Evaluation and decision

Use optional Qt Widgets for the first desktop shell and Qt Multimedia's FFmpeg backend
inside a separate process. The executed prototype covers WAV audio, fractional-rate
MPEG-4/MP4, FFV1/PCM Matroska and irregular-frame-timestamp Matroska. It measures decoded
frame intervals after seeks, pause behavior, audio/video delivery timestamps, worker
cancellation and rapid seek replacement. This evidence precedes the desktop integration.
See [playback evaluation](../playback-evaluation.md) for measured results and limits.

| Candidate | Decision for this slice |
| --- | --- |
| Qt Multimedia / FFmpeg | Selected after the local corpus passed. Provides decoding, timestamp-based seeking and embedded audio scheduling with the selected toolkit. |
| Direct FFmpeg libraries | Retained as an option for later precision playback/export; implementing audio-device scheduling and a frame queue is not justified by this slice. |
| Qt native media backends | Not selected. Qt documents Windows Media Foundation deprecation and limited support for native backends relative to FFmpeg. |
| GStreamer/GES | Documentation comparison only; no runtime prototype was executed. Revisit when multi-track scheduling warrants evaluating a different adapter. |
| Qt Quick/QML | Viable future UI option; Widgets provides the small inspector/timeline without a declarative runtime dependency. |

This is not a comparative benchmark of all candidates. Qt Multimedia is not adopted as
an authoritative project/timeline model. The public preview plan remains plain C++ values.
All UI edits execute the same Editor/Transaction commands used by the CLI.

[Qt backend guidance](https://doc.qt.io/qt-6.10/qtmultimedia-index.html).
[QMediaPlayer API](https://doc.qt.io/qt-6.10/qmediaplayer.html).

## Process and timing contract

A preview worker owns QMediaPlayer, QVideoSink, QAudioBufferOutput and QAudioOutput. The
editor owns a QProcess and a random, user-access-only local socket. It receives bounded
JSON lines with timestamps and resized JPEG images. The worker emits embedded audio
through the default output when enabled. Tests inspect decoded audio buffers with output
muted/absent; they do not measure speakers, hardware latency or display scanout.

Seek/open cancels the old worker, discards its socket/output, and launches only the latest
requested position after exit. There is at most one worker. Loading has a deadline and
playing has a progress watchdog; cancellation kills the worker without synchronously waiting
in the UI action. Normal teardown waits briefly for process completion. OS scheduling and
process termination are not hard real-time guarantees. This is supervision, not a security
sandbox or crash-recovery protocol.

Preview uses a single populated track, including a clip's embedded audio. Gaps are silent
and blank; the preview clock pauses while the next source loads at a cut. Independent
track mixing, seamless transitions and sample-accurate audio cuts remain deferred.

The native model still normalizes each stream's source range independently. Accordingly,
nonzero stream starts, unknown video/A/V starts and multiple streams of a kind are rejected for
preview with a clear error, while remaining valid editable assets. This avoids silently
changing the model's source convention to container time. A future source-origin migration
can broaden support after explicit tests.

Stored edit times remain rational. Only the adapter seek boundary floors to milliseconds.
VFR is sought by media timestamps, not an average-fps-derived frame number. Reported frame
start/end timestamps are observations; a nominal frame end can precede the next presentation
timestamp. Between sparse frames Qt can select the next frame (0.583 s for a 0.550 s request
in the corpus). The adapter deliberately exposes approximate preview rather than claiming
an exact held-frame index. No complete frame index or exact-frame stepping
claim is made. Preview is limited to 24-hour timeline/source positions and millisecond-representable clips.

## Versions, licensing and build configuration

Local SDK: Qt 6.10.3, Windows MSVC2022 x64, downloaded from Qt's package servers using
aqtinstall 3.3.0 into ignored build/tools. The optional modules used are Core, Gui, Widgets,
Network, Concurrent and Multimedia. The application dynamically links Qt; no Qt/FFmpeg
headers, sources or binaries are committed. Base command-line builds need no Qt SDK.

Qt Widgets is available under LGPLv3 (or alternative commercial/GPL terms). Qt Multimedia
also has open-source licensing and third-party notices. The project's original source
remains MIT; this does not relicense dependencies. Future binary distribution must supply
applicable notices, matching source access and replacement/relinking rights rather than
assuming the application's MIT license covers the assembled binaries.
[Qt Widgets licensing](https://doc.qt.io/qt-6.10/qtwidgets-index.html#licenses),
[Qt licensing](https://doc.qt.io/qt-6/licensing.html).

The actual local Qt runtime reports FFmpeg 7.1.3 with shared libraries, no GPL/nonfree
configure switches, and this configuration:

~~~text
--prefix=/c/FFmpeg-n7.1.3/build/msvc/installed --disable-programs --disable-doc --disable-debug --enable-network --disable-lzma --enable-pic --disable-vulkan --disable-v4l2-m2m --disable-decoder=truemotion1 --enable-zlib --extra-cflags='-IC:/zlib-1.3.1/build/amd64' --extra-ldflags='-LIBPATH:C:/zlib-1.3.1/build/amd64' --toolchain=msvc --enable-shared --disable-static
~~~

This is distinct from the external Gyan FFmpeg 9.0.1 GPL-enabled probe/fixture tools from
Milestone 3. Qt identifies its provided FFmpeg build and source/build-script references in
[its FFmpeg attribution](https://doc.qt.io/qt-6.10/qtmultimedia-attribution-ffmpeg.html).
Actual libraries/configurations on other platforms must be verified in their build context.

The worker fixes QT_MEDIA_BACKEND=ffmpeg, limits nested protocols to file, and uses software
decoding/texture conversion for this measured baseline. Those tuning environment variables
are Qt private API and require revalidation on SDK upgrades.
[Qt FFmpeg configuration](https://doc.qt.io/qt-6.10/advanced-ffmpeg-configuration.html).

## Remaining boundaries

No rendering/export, color-managed/HDR display, proxy generation, audio mixing, frame-accurate
stepping, arbitrary start-offset remapping or professional-scale timeline interaction is
introduced. Images are a resized JPEG preview. Seek diagnostics describe decoded timestamps;
they do not certify end-to-end audiovisual synchronization on the user's hardware.
