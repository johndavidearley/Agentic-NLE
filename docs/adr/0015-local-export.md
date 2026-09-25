# ADR 0015: Fixed-revision local export

Status: Windows synthetic media acceptance, desktop workflow and bounded disk-full behavior passed
on the pinned development runtime. Unavailable-encoder behavior and Linux/macOS export acceptance
remain open. See [the evaluation](../export-evaluation.md).

The renderer receives a detached playback::SequencePlan copied from a project snapshot. It uses
the same evaluator, original media, stream routing, source clock and 48 kHz stereo mixer as
sequence playback. An export never consults the live editor after the plan is built. Source
identity is checked while rendering and again before the staged file can replace its destination.

The reference container is Matroska with FFV1 video in BGR0 and packed 32-bit float PCM at 48 kHz
stereo. BGR0 is the lossless pixel format supported by the pinned FFmpeg 7.1.3 runtime. FFV1 is
specified as a lossless intra-frame codec by the FFV1 specification
(https://ffmpeg.org/~michael/ffv1.html). Container encoding follows the pinned FFmpeg 7.1 API and
its muxing example (https://www.ffmpeg.org/doxygen/7.1/mux_8c-example.html).

The delivery preset is MP4 with H.264 and AAC-LC at 192 kbit/s. It selects libx264 at CRF 18,
medium preset when available. On Windows it falls back to the Media Foundation h264_mf encoder at
quality 85; other platforms currently require libx264. The actual encoder and preset are returned
in the job result and recorded in container metadata. MP4 also records the captured project
revision, sequence ID and BT.709 SDR profile. AAC output carries end-padding metadata and uses a
48 kHz movie timescale so decoded output retains the renderer's exact sample count. Encoder
availability depends on the linked runtime; the implementation does not bundle encoder binaries.
The core, default CLI and Qt-free MCP remain independent of FFmpeg. Export is built only with the
existing optional desktop/FFmpeg SDK.

The supported output profile is progressive SDR at the sequence's even dimensions up to
3840x2160, 1–60 fps, and 48 kHz stereo. The renderer decodes original sources no larger than
1920x1080, applies the existing centered aspect-preserving fit on black, and rejects HDR transfer
functions, nonzero display rotation, unavailable streams, changed sources and profiles outside
the existing two-video/four-audio-track playback limits. RGB is tagged BT.709 SDR; unspecified
source color tags are assumed BT.709, while explicitly non-BT.709 primaries, transfer or matrix
tags are rejected. No HDR tone mapping, gamut conversion, effects or transition rendering is
implied. Sequence end produces ceil(duration * 48000) PCM samples and every output-grid frame with
timestamp strictly before sequence end.

Output is written to a unique sibling staging directory. A cancelled, failed or unsupported job
removes staging and leaves a pre-existing destination intact. Replacing a destination requires an
explicit overwrite option; installation happens atomically after encoding and a final source
check. No power-loss durability promise is added.

The Windows synthetic acceptance covers independent decode, frame/sample counts, source origins,
black gaps, encoded error metrics, exact AAC sample boundaries, metadata, cancellation, changed
media, overwrite refusal and an install failure. A separate manual desktop pass and an actual
8 MiB output-filesystem exhaustion check also passed. These do not establish cross-platform
support. The remaining matrix is tracked in the evaluation and Milestone 10 plan.
The deployment must document its FFmpeg build and honor the licenses and source obligations for
that build; local presets do not authorize redistribution.
