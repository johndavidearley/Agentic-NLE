# Roadmap

## Milestone 1 — Headless Timeline Core (complete)

Typed IDs; rational time; tracks/media; insert, move, trim, split and delete;
undo/redo; snapshots; native persistence; CLI; invariant tests; build and architecture.

## Milestone 2 — Hardened Editing Sessions (complete)

Grouped preview/commit/rollback transactions; stale-revision rejection; synchronized
Editor entry points; persisted actor/operation metadata; pull-based edit notifications;
history entry/byte limits; undoable track deletion/reordering and media relinking.

Native version 2 explicitly migrates version 1 while preserving object IDs and allocation
watermarks. Single-authority identity/UUID policy is recorded in ADR 0008.
Tests include competing writers, failed/stale/foreign batches, history pruning, migration,
2,000 deterministic malformed-file mutations and Windows replacement-lock failure.
CI configuration covers Windows, Linux, and macOS (Apple Silicon and Intel), with GCC/Clang/MSVC/Apple Clang and Linux address/undefined-behavior sanitizers.
See performance.md for local measurements and exact verification scope.

Exit demonstrated: batches preview, commit and undo/redo as one edit; failed/stale batches
leave live state unchanged; identity/attribution survive save/load; retention and latency
budgets are measured. Open transactions/history are intentionally not persisted.

## Milestone 3 — Media Probing and Source Metadata (complete)

Optional external ffprobe adapter, backend-neutral stream facts, exact integer timing where
available, labelled container estimates, source availability, command-based import and
verified relink with stable IDs/undo/redo. Version 3 persists metadata and migrates versions
1 and 2. Supervised processes support bounded output, timeout and cancellation.

The bounded backend decision combines documentation evaluation of FFmpeg/GStreamer/GES
with an executed ffprobe corpus; GStreamer was not run. This chooses discovery only,
not a playback engine. Exact tool configuration and evidence are recorded in
[media evaluation](media-evaluation.txt) and [ADR 0009](adr/0009-media-probing.md).

Exit demonstrated locally: generated WAV, fractional-rate MP4 and A/V Matroska probe/import/
relink; metadata survives persistence; invalid/short relinks preserve the project; Unicode,
offline sources, subprocess failure, timeout and cancellation have tests. Debug/Release
verification is on Windows. CI configuration also runs the corpus on Linux and both macOS
architectures; those executions remain to be verified by CI.

## Milestone 4 — Minimal Playback Vertical Slice (complete)

An optional Qt Widgets desktop imports media in the background, appends clips as a grouped
transaction, previews a sequence, and exposes trim/position, split, delete, undo/redo and
native save/load. A headless preview plan maps exact timeline/source ranges; a supervised
Qt Multimedia worker provides open, play/pause, seek and time observations.

The selected Qt/FFmpeg adapter was exercised against audio, fractional-rate video, embedded
A/V and VFR fixtures. Offset/unknown video origins and multiple populated tracks reject
preview explicitly. Missing/changed sources, decode/start failure, hung workers, rapid seek
replacement, cuts and blank/silent gaps are covered. All edits keep the model authoritative.
[ADR 0010](adr/0010-desktop-playback.md) records licensing, alternatives and bounded adoption.

Exit demonstrated on Windows in Debug/Release: imported media plays/seeks, decoded timestamp
intervals cover corpus seeks, cancellation is measured, and desktop edits share validated
undoable commands. The deployed Windows runtime also passes the desktop smoke test.
[Playback evaluation](playback-evaluation.md) records exact results and limits. CI is
configured for Linux and macOS (Apple Silicon and Intel); those executions remain unverified
from this workspace. This slice does not promise seamless cuts or physical A/V sync.

## Recommended Milestone 5 — Source Origins and Precision Seeking

Define a common source-time convention that preserves stream offsets, with explicit native
migration and relink behavior. Build decoded-frame lookup and stepping on CFR/VFR fixtures,
then measure seek correctness and A/V scheduling over longer clips. Extend the existing
preview adapter only after evidence supports the new source-time contract.

Exit: nonzero/unequal stream starts preserve their intended alignment; frame stepping and
seek selection are demonstrated on timestamped fixtures; old projects migrate predictably.
Independent track mixing, export and a broad effects system remain separate work.

## Later work

Seamless/multi-track playback; small MCP adapter with authorization/idempotency; interchange
and explicit lossy conversion reporting. Audit compaction, independent-writer save conflicts,
UUID remapping, more efficient history and crash durability need separate bounded work as
their use cases arrive. Color, compositing, multicam, broad effects, advanced audio and
collaboration remain out of scope.
