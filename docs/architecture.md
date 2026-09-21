# Architecture

Implemented through Milestone 9, including provisional timeline editing and derived media caches.
[Production validation](multitrack-evaluation.md) distinguishes local evidence from platform CI.
[Recovery validation](recovery-evaluation.md) records the preceding milestone platform matrix.

~~~text
Qt desktop -----+
CLI ------------+--> Editor / Transaction --> shared command application --> candidate
MCP stdio ------+          |                          |
                     expected revision         validate invariants
                           |                          |
                     atomic commit + bounded undo history
                           |
                     detached snapshot + durable operation records
                           |
                     native version 5 persistence
~~~

## Boundaries

src/core owns exact rational time and domain errors. src/project owns detached project
DTOs, validation, attribution records and persistence. src/commands owns sessions,
transactions and typed commands. src/media owns the optional external ffprobe adapter and supervised process runner.
src/cli exercises editing and source discovery without Qt or display decoding.
src/playback builds immutable preview plans from detached snapshots and maps rational
timeline positions to source ranges without Qt. src/desktop owns Widgets, asynchronous
import, transport supervision and the timeline widget. The pure commands/timeline helpers
translate gestures into existing typed commands using exact rational snapping. Each gesture
validates detached transaction candidates; only release publishes a command with its captured
revision. Provisional moves do not enter the live history or audit log.
The desktop media cache supervises two separate bounded decode processes, revalidates source
identity outside the UI thread and discards obsolete generations. Cache data is disposable;
see [timeline editing](timeline-editing.md) for limits. src/decode owns the optional FFmpeg decoder/renderer;
its supervised desktop worker owns bounded queues and Qt audio output.
There are no protocol, media backend or GUI dependencies in the command engine.

Editor owns live state and immutable shared history entries. Its public entry points are
mutex-protected. Transactions are private candidate copies with a weak owner identity
and a base revision; their staged edits use exactly the same application function as
direct commands. Both paths validate before publication. Failure preserves live state.
See [editing sessions](editing-sessions.md) for lifecycle and concurrency guarantees.

History entries contain before/after content without duplicated audit logs. Commit
prepares the updated project, operation record and bounded history before publishing.
Undo/redo restore content and the highest allocation watermark, then append a new
revision/attribution record. Inspection and notifications return detached values.

## Identity, ordering and time

Project IDs are random nonzero 64-bit identifiers. Object IDs are strong C++ types and
project-local monotonic integers independent of paths and vector positions. Operation IDs
use a separate typed namespace indexed by persisted revision. Published references must
include project ID. Preview IDs are provisional until commit.
[ADR 0008](adr/0008-identity-and-migration.md) defines single-authority identity and migration policy.

Sequences, tracks and assets preserve order; ReorderTrack explicitly changes track order.
Clips sort by exact position then ID. IDs remain stable during move, trim, relink and undo.

RationalTime stores reduced nonnegative seconds as checked int64 fractions.
One 24000/1001 fps frame is 1001/24000 seconds; a 48 kHz sample is 1/48000.
Sequence frame duration preserves the intended grid independently of normalization.
Comparison avoids overflowing cross products; arithmetic rejects unsupported intermediates.
SourceTime separately represents signed media origins; edit coordinates stay nonnegative.
Snapping, timecode and speed changes remain deferred.

Clips use positive-duration half-open ranges at speed 1. Source bounds and media/track
compatibility must hold, and same-track overlaps are rejected. Moves may cross sequences.
Trims explicitly specify position and source range. Split preserves the left ID and
allocates a right ID. DeleteClip does not ripple; DeleteTrack cascades clips but retains media.

## Media, provenance and persistence

A logical asset owns ID, name, declared stream capability and duration. Original/proxy
locators are replaceable. SourceMetadata holds optional validated probe observations.
The adapter probes outside Editor, then creates RegisterMedia or ReplaceMediaSource commands.
Verified replacement checks kind and all clip bounds; ordinary original locator edits clear
stale source metadata. Both paths preserve stable IDs and are undoable. File availability is
computed separately on inspection. See [media probing](media-probing.md).

Native version 5 adds sequence output, track playback and clip stream routing settings.
Versions 1–4 migrate explicitly while preserving IDs, clip coordinates, frame rates, embedded
audio and the source-clock convention. Unknown versions fail. No history is invented for imported
old projects. Attribution records survive undo and save/load, but are not an authenticated
or replayable event journal. Persistence consumes snapshots and atomically replaces a
fully staged file during ordinary operation; power-loss durability is not promised.
See [native format](native-format.md).

## Desktop and read-only preview

The desktop's import, append, trim/position, split, delete and undo/redo actions all use
Editor or Transaction with human attribution and expected revisions. Probe work runs
outside the UI thread; a changed project/revision rejects its stale result. Saving uses
the shared DocumentFile service with cooperative ownership and expected-file checks. Two
validated checkpoint slots support explicit recovery; see [project recovery](project-recovery.md). No UI state, active transport or undo stack is needed to reopen a project.

A validated sequence plan is a detached, revision-labelled snapshot. Exact evaluation selects
the top active enabled video and every enabled audio contribution, including audio beneath
covered video. Track settings and explicit stream routing use typed undoable commands;
sequence output settings and split-preserved clip routing are persisted in version 5.

The Qt transport sends the fixed snapshot to a supervised worker over a private local socket.
A producer decodes through FFmpeg, mixes stereo float PCM, and prepares preview images before
queueing. One QAudioSink processed-sample clock drives audible playback; silent tests use one
monotonic clock. Video follows the same sample clock. A 240 ms prebuffer and bounded 320 ms
queue decode across cuts. The clock thread handles audio feeding and prepared image delivery.

Core edit times remain exact fractions. Shared source origins retain relative stream starts
and their union of spans; legacy assets keep their earlier convention. Video selects the
held decoded PTS at each exact output frame time. Paused seeking selects at the requested
time; frame stepping follows the sequence output grid. Audio is resampled to 48 kHz stereo,
mixed in track order with explicit gain, then hard-clipped. Source timing and stream routing
stay independent of visible-video priority. Negative origins decode from original media.

Cancel, seek, pause or edit invalidates old output and kills the worker; rapid seeks coalesce
to the latest request. Source identity/mtime checks, bounded local socket transfers, decoder
deadlines and a playback watchdog contain failures without changing the project. The worker
provides process isolation, not a security sandbox. Long audio-source seeks remain deadline
bounded because decoding from the origin preserves a stable resampling/sample anchor.

The old single-track Qt player and its frame-index/negative-packet-copy adapter remain only
for regression tests and the recorded backend comparison. They are no longer the desktop's
sequence playback path. See [desktop preview](desktop-preview.md),
[production evaluation](multitrack-evaluation.md) and [ADR 0014](adr/0014-multitrack-backend.md).

## Local agent adapter

src/mcp owns JSON wire conversion, schema discovery, stdio lifecycle and a bounded session
registry for proposals and retry results. Its launcher owns the project path, fixed actor,
permissions and cooperative save lock. JSON/MCP do not enter nle_core. Agent edits use the
existing Editor/Transaction API and preserve version-5 playback settings and exact source clocks.
Sessions inspect existing registered media; no backend, Qt or media file access is required.
See [MCP boundary](mcp-boundary.md) for revision, authorization, persistence and retry limits.

## Scaling and remaining boundaries

Snapshot history remains O(project size) per retained edit. Default retention is 100 entries
and 64 MiB of accounted payload. A single oversized history item is rejected, and callers
can explicitly disable history. Each transaction stages at most 1,024 commands.
The audit cap is 10,000 durable operations. There is no silent loss of audit metadata.

The [benchmark](performance.md) measures 100-command transactions on 1,000/10,000 clips.
This validates a bounded milestone workload, not professional-scale performance.
Independent live Editors, distributed merges, remote authentication, durable retry replay,
general render determinism, export and advanced ripple/group timeline interaction remain outside
this implementation. MCP has process-local retry protection; desktop/CLI/MCP writes share
cooperative file ownership, and recovery restores checkpoints without restoring sessions.
