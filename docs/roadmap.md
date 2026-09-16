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

## Milestone 5 — Source Origins and Precision Seeking (complete)

A signed media-origin type and shared source clock preserve positive/negative A/V origins,
relative stream starts and tails. Native version 4 records the clock convention explicitly;
versions 1-3 retain old clip coordinates and legacy timing. Verified relink preserves the
asset's clock mode and validates all clip bounds.

Decoded-frame lookup selects paused frames by presentation timestamps and adds previous/next
frame controls for CFR, VFR and B-frame fixtures. The requested playhead remains exact.
Negative packet origins use a bounded temporary packet-copy source only after stream/frame
verification. Original media and persisted locators stay authoritative.
[ADR 0011](adr/0011-source-origins-and-frame-index.md) records the decision and limits.

Exit demonstrated on Windows: 33/33 tests in both Debug and Release, 27/27 in the independent
headless Release build, and the deployed desktop smoke test. Offset/negative sources retain
alignment; frame seeks and stepping match decoded timestamps within integer rescaling;
legacy projects migrate predictably. A 12-second delayed-audio fixture measures decoder
scheduling through 11 seconds. [Precision evaluation](precision-evaluation.md) records exact
results, bounds and remaining platform validation. Linux/macOS CI is configured and updated
for the observed baseline failures; the later Milestone 7 run passed all configured platforms.
See [the follow-up evidence](recovery-evaluation.md).
Independent track mixing, seamless cuts, export and broad effects remain separate work.

## Milestone 6 — Local Agent Editing through MCP (complete)

An optional, headless stdio MCP adapter exposes project inspection, paged changes,
preview/commit/rollback of grouped timeline commands, undo/redo and explicit save. Agents
operate on already registered media using the same Editor/Transaction engine as the desktop.
The launcher binds one project, one actor and explicit edit/save permissions. JSON uses exact
decimal-string IDs/revisions and rational time pairs. No network listener or arbitrary file
path tools are exposed.

Exit demonstrated on Windows: 36/36 tests in Debug and Release; 30/30 in an independent
Qt-free MCP Release build; 27/27 in the default build. The official MCP SDK 2.2.0 discovers
nine tools and executes previews, grouped commits, undo/redo and explicit save. Agent batches
equal direct core commands; stale, invalid, foreign, expired, unauthorized and duplicate
requests have tested outcomes. Save is bound to the configured file and detects ordinary
external changes. Retry records are bounded and never silently evicted. The native format
and core remain unchanged. [MCP evaluation](mcp-evaluation.md) records results and limits.
All eight cross-platform MCP Debug/Release jobs subsequently passed in
[Milestone 7 validation](recovery-evaluation.md). Multi-track playback, agent
media import, remote transport and concurrent desktop/server editing remain separate work.

## Planned next phase — a usable editing workflow

Proposed 2026-09-15; Milestone 7 completed 2026-09-16. Later milestones remain planned. The recommended sequence first completes
human editing and export, then integrates agent media preparation and the open desktop session.
See [the detailed plan](next-milestones.md) for scope, dependencies, acceptance tests and limits.

| Milestone | Main outcome | Status |
| --- | --- | --- |
| 7 — Reliable Projects and Recovery | Verified platform baseline, coordinated saves and recoverable desktop work | Complete |
| 8 — Multi-track Playback | B-roll, dialogue and music play together with defined routing and timing | Planned |
| 9 — Practical Timeline Editing | Zoom, scroll, drag, trim, snap, thumbnails and waveforms | Planned |
| 10 — Export a Finished Video | A validated output file matching the supported sequence | Planned |
| 11 — Agent Media Preparation | Approved import/relink and observable export jobs through MCP | Planned |
| 12 — Shared Human and Agent Editing | One live project, visible proposals and one undo history | Planned |
| 13 — Installable Alpha | Packaged and verified complete workflows on the supported platforms | Planned |

Milestone 7 is complete: protected document saves, bounded recovery checkpoints and explicit
recovery choices are verified with process termination, corrupt-checkpoint fallback and
conflicting writers. All 28 platform CI jobs passed on `b2978a0`. See
[recovery behavior](project-recovery.md) and [validation results](recovery-evaluation.md). Milestone 8 begins with a bounded backend
prototype before adopting a multi-track implementation. Milestone 10 completes the first
human import/edit/export workflow; Milestone 12 completes the shared local agent workflow.
Each milestone needs its own measured exit evidence before it can be marked complete.

## Later work

Interchange with explicit loss reporting and identity remapping; proxies and larger projects;
ripple/linked editing; titles, captions, transitions and basic color tools. Audit compaction,
more efficient history and power-loss durability need separate measured and versioned work.
Multicam, broad effects, advanced audio, AI media generation, cloud collaboration and a plugin
marketplace remain beyond the planned phase. Reassess priorities after alpha use.
