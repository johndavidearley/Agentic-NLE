# ADR 0011: Shared source origins and decoded frame lookup

Status: accepted for Milestone 5, 2026-09-14. Extends ADRs 0002, 0009 and 0010.

## Decision

Keep edit positions/durations nonnegative and exact. Introduce a separate signed SourceTime
for media timestamps. New sources with known origins use one clock whose zero is the earliest
A/V start. Preserve stream offsets and the union of their spans. Keep legacy asset semantics
explicit in native version 4; loading older projects does not reinterpret clip coordinates.
Verified relink retains the asset's convention and validates replacement bounds atomically.
See [native format](../native-format.md) for the complete migration contract.

A bounded ffprobe decoded-frame index provides exact held-frame lookup: a frame covers its
presentation timestamp through the next frame's timestamp; the last frame uses its decoded
duration. Lookup and stepping use rational source times, not an average-fps calculation.
The index requires increasing known timestamps and a known final duration. Unsupported or
incomplete indexes report an error rather than using an approximate frame number.

The Qt worker retrieves the selected paused frame by seeking inside its nominal interval and
verifying the decoded timestamp against the index (integer microsecond rescaling tolerance).
The requested playhead remains exact and independent of the temporary decoder seek position.
Previous/Next Frame pauses playback and visits source-frame boundaries inside the current
clip, including stepping backward from sequence end. Gaps and unrelated tracks are not
implicitly traversed. Live audio/video scheduling remains owned by Qt.

## Negative origins

The executed negative-origin Matroska prototype showed Qt skipping packets before source
zero despite reporting normalized frame timestamps. For such sources only, create a temporary
Matroska packet-copy file with nonnegative timestamps using external ffmpeg. Verify stream
alignment, duration (within one millisecond of container estimates), every decoded frame
position/end and frame count against the original before using it. No re-encoding occurs.
The original file, project locators, source clock and frame index remain authoritative.
Sources that cannot preserve this evidence reject preview with an explicit error.

One cached temporary source is retained per transport and removed when replaced or destroyed.
The operation is part of frame preparation, supervised under the same 30-second total deadline
and cancellation flag. A 512 MiB output cap (with at most a 1 MiB packet overrun accepted)
limits temporary disk use; comparison rejects truncated outputs. This is a playback adapter
workaround, not a persisted proxy workflow or an export feature.

## Alternatives and boundaries

Average-rate frame arithmetic fails on VFR. QVideoFrame's nominal end timestamp also does not
define a held frame through the next presentation time. Direct Qt seek selected a future frame
for the Milestone 4 sparse-frame test; indexed selection fixes that case without replacing
Qt's entire A/V scheduler. A direct libav decoder/frame cache is a future alternative if the
verified Qt retrieval path becomes a bottleneck. No new third-party library is introduced.

Frame preparation runs outside the UI thread. It is cancelled/coalesced independently of the
preview worker. A cache matches path, size, modification time and source metadata; changes
invalidate it. The frame limit is 200,000; process output is capped at 16 MiB. The cache is
session-local, not a media fingerprint or a persisted index. Core commands remain Qt-free.

This milestone does not promise seamless cuts, sample-accurate audio boundaries, physical
speaker/display synchronization, professional-scale indexing latency, multi-track mixing,
color-managed monitoring or export. Evidence covers the generated corpus and software decode
baseline; SDK/backend upgrades require revalidation.

## Dependencies and evidence

Use the existing optional ffprobe tool for frame discovery. Negative-origin preview also uses
the ffmpeg executable next to ffprobe (or on PATH). Both remain external processes with their
build-specific licenses; the local 9.0.1 tools are GPL-enabled. Qt 6.10.3 / FFmpeg 7.1.3 remain
the dynamically linked playback runtime. The original application is MIT licensed; binary
redistribution must account separately for the Qt and selected FFmpeg builds as recorded in
ADRs 0009/0010. No dependency binaries or third-party sources are committed.

Primary references: [ffprobe frame output](https://ffmpeg.org/ffprobe.html),
[Qt media-player position API](https://doc.qt.io/qt-6.10/qmediaplayer.html),
[FFmpeg stream copy](https://ffmpeg.org/ffmpeg.html#Streamcopy).
The [Milestone 5 evaluation](../precision-evaluation.md) records executed checks and timing
observations, including the limits of decoder-delivery measurements.
