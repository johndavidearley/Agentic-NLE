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

## Recommended Milestone 4 — Minimal Playback Vertical Slice

Evaluate seeking and A/V synchronization on the shared corpus before choosing the playback
adapter. Establish a small read-only preview transport (open, play/pause, seek, time position)
and a minimal Qt timeline shell using the existing command API for edits. Resolve source
start offsets, variable-frame-rate lookup, error reporting and offline behavior explicitly.
Keep the native model authoritative and document dependency/build licensing before adoption.

Exit: one imported sequence can be inspected, played and sought with measured timing and
bounded cancellation; a UI edit uses the same validated undoable commands as the CLI.
Rendering/export, broad effects and a large MCP surface remain later work.

## Later work

Minimal playback and Qt timeline; small MCP adapter with authorization/idempotency;
interchange and explicit lossy conversion reporting. Audit compaction, independent-writer
save conflicts, UUID remapping, more efficient history and crash durability need separate
bounded work as their use cases arrive. Color, compositing, multicam, broad effects,
advanced audio and collaboration remain out of scope.
