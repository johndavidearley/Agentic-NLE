# ADR 0005: Defer media backend and desktop selection

Status: probing superseded by ADR 0009; bounded playback/UI selected in ADR 0010. Date: 2026-09-09.

Current needs are logical assets, declared kinds/durations and optional original/proxy
locators. Offline media is valid. Choose decoding/playback after measuring seek accuracy,
A/V synchronization, cancellation, packaging, hardware and deterministic render needs.

FFmpeg is the first probing/codec candidate. Its
[official legal page](https://ffmpeg.org/legal.html) describes LGPL-2.1-or-later licensing
and optional GPL components. Record exact build options, dependencies and source provenance
for eventual distribution. Backend choice does not settle codec/patent obligations.

GStreamer/GES is an alternative for pipeline scheduling and editing playback.
The [GStreamer licensing advisory](https://gstreamer.freedesktop.org/documentation/application-development/appendix/licensing.html)
describes LGPL licensing and additional plugin concerns. Test whether its scheduling
saves work without imposing a second authoritative model.

Qt 6/QML is the leading desktop candidate.
[Qt documents component-specific licensing](https://doc.qt.io/qt-6/licensing.html);
review modules and distribution obligations before adoption. Native platform UI would
duplicate implementation effort across platforms.

Prototype adapters separately with a shared corpus. Do not introduce playback/render
placeholders or abstract backend interfaces before a concrete implementation needs them.
