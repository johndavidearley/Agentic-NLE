# Roadmap

## Milestone 1 — Headless Timeline Core

Implemented: typed IDs; rational time; video/audio tracks; logical media registration;
insert, move, trim, split and delete; undo/redo; detached inspection; native save/load;
CLI demo; invariant tests; build, formatting, warnings and CI configuration; architecture ADRs.

No decoding, desktop UI, playback or live MCP server is included.

## Recommended Milestone 2 — Hardened Editing Sessions

Add grouped transactions with preview/commit/rollback, revisions, actor/operation metadata,
history limits and edit notifications. Add track deletion/reordering and media relinking
only with explicit undo semantics. Expand persistence fuzzing, compiler coverage,
large-project benchmarks and failure testing. Decide UUID policy and schema migrations.

Exit: batches preview and commit as one undoable unit; failed/stale batches have no
effect; identity survives session/history boundaries; memory/latency budgets are measured.

## Subsequent bounded milestones

1. Media probing: compare FFmpeg and GStreamer/GES with a shared corpus and explicit
   licensing/build configurations. Keep the native model independent.
2. Minimal playback and Qt timeline: establish A/V clock, seek, proxy and snapping
   policies. Human edits use the command API.
3. Small MCP adapter: inspection, a few edits and transactions, with revision/idempotency
   rules and the same editing session as the UI.
4. Native-format evolution and OTIO interchange: report or reject lossy conversions.

Color, compositing, multicam, advanced audio, collaboration and broad effects remain
future implementation work until the editing foundation is proven.
