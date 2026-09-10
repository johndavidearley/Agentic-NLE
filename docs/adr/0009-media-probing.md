# ADR 0009: Optional ffprobe adapter for source discovery

Status: accepted for Milestone 3, 2026-09-10. Extends ADR 0005 for probing only.
Playback, rendering and desktop toolkit selection remain open.

## Bounded evaluation

The decision covers local source discovery without a display pipeline. It does not
choose a final playback engine or claim comparative decoder performance.

| Candidate | Fit for this milestone | Cost / remaining evidence |
| --- | --- | --- |
| External ffprobe | Structured stream/format fields, integer ticks/time bases, codec and frame-rate descriptors; independently supervised executable | Small process/parser adapter; executable packaging and build-specific codecs remain external |
| Linked libavformat/libavcodec | Direct typed metadata, no text parsing | Adds headers, linked libraries and ABI/distribution work before playback requires them |
| GStreamer GstDiscoverer | URI discovery, stream information, synchronous and asynchronous APIs | Requires GLib/GStreamer runtime and selected plugins; async API uses a main context; no installed runtime was available for this Windows experiment |
| GES | Candidate for later editing/playback scheduling | A scheduling/editing framework is outside discovery scope; native project commands must remain authoritative |

This is a documentation comparison plus an executed ffprobe prototype, not an
apples-to-apples runtime benchmark. GStreamer/GES were not run. The generated corpus
in tools/media_corpus.py is reusable for the later playback comparison; record exact
plugin lists and versions when running that comparison. Lack of an installed runtime
is a limit on evidence, not evidence that GStreamer cannot support the files.

Primary references: [ffprobe output/options](https://ffmpeg.org/ffprobe.html),
[GstDiscoverer API](https://gstreamer.freedesktop.org/documentation/pbutils/gstdiscoverer.html),
[GES](https://gstreamer.freedesktop.org/documentation/gst-editing-services/index.html).

## Decision and integration

Choose the external ffprobe adapter. nle_media depends on nle_core; nle_core has no
backend dependency. Selected sectioned output requires no third-party parser library.
Probe outside the editor lock, then create RegisterMedia or ReplaceMediaSource commands.
No backend-specific object or handle enters a project snapshot. There is no new abstract
backend hierarchy. The adapter can be replaced without migrating the timeline model.

Runtime defaults to an executable found in absolute PATH directories. Callers can supply
an explicit trusted path. CMake never downloads it. Integration tests explicitly require
both ffmpeg (fixture generation only) and ffprobe; default core tests need neither.

The local Windows experiment used Gyan 9.0.1 essentials, downloaded through the provider
linked by the [official FFmpeg download page](https://ffmpeg.org/download.html).
The zip was stored under ignored build/tools, not bundled with this repository.
Zip SHA-256: FEC81AE03971D9DD4BE3EBE02E263BD2EC1D789483F931BDBA5F5715E65DA2E9.
The exact executable version/configuration is recorded in media-evaluation.txt and in
each probed source. CI prints its installed build configuration separately.

## Licensing and distribution boundary

Our original adapter/core code remains MIT. No FFmpeg or GStreamer source, headers,
libraries or binaries are committed. External-process isolation is an engineering
boundary, not a claim that distribution obligations disappear.

FFmpeg's base license is LGPL-2.1-or-later; enabling optional GPL components changes the
applicable license. The tested build reports --enable-gpl --enable-version3 --enable-static,
with many optional codec libraries; it is not an LGPL-only build.
Any future redistributed binary requires its exact source/configuration, notices and
applicable obligations to be reviewed for that distribution.
[FFmpeg licensing](https://ffmpeg.org/legal.html).

GStreamer uses LGPL with plugin-specific considerations; a future prototype must record
its actual runtime, plugin packages and license inventory, rather than treating the
framework license as covering every plugin.
[GStreamer advisory](https://gstreamer.freedesktop.org/documentation/application-development/appendix/licensing.html).

## Consequences

The command core remains buildable without media tools. Discovery may fail for unsupported
containers, missing codec support, unavailable duration or configured resource bounds;
these failures are explicit and publish no edit. Stream timing is retained exactly where
available, while fallback container estimates are identified. Average frame rate is not
a promise of constant-rate frame indexing. Playback synchronization, seek accuracy,
color/HDR/rotation, proxies and content-identity matching need later decisions.
