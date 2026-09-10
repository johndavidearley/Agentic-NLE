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

## Recommended Milestone 3 — Media Probing and Source Metadata

Choose a probing backend through a bounded FFmpeg vs GStreamer/GES evaluation with
explicit licensing/build configurations and a shared media corpus. Import real duration,
frame rate, stream metadata and offline/relink status into stable logical assets.
Preserve the existing native model and command boundary.

Exit: supported media can be probed/imported/relinked with precise metadata and clear
errors without decoding video for display. No Qt timeline or rendering pipeline yet.

## Later work

Minimal playback and Qt timeline; small MCP adapter with authorization/idempotency;
interchange and explicit lossy conversion reporting. Audit compaction, independent-writer
save conflicts, UUID remapping, more efficient history and crash durability need separate
bounded work as their use cases arrive. Color, compositing, multicam, broad effects,
advanced audio and collaboration remain out of scope.
