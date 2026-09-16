# Architecture

Implemented through Milestone 7, including shared document ownership and recovery.
[Validation](recovery-evaluation.md) records the completed platform matrix.

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
                     native version 4 persistence
~~~

## Boundaries

src/core owns exact rational time and domain errors. src/project owns detached project
DTOs, validation, attribution records and persistence. src/commands owns sessions,
transactions and typed commands. src/media owns the optional external ffprobe adapter and supervised process runner.
src/cli exercises editing and source discovery without Qt or display decoding.
src/playback builds immutable preview plans from detached snapshots and maps rational
timeline positions to source ranges without Qt. src/desktop owns Widgets, asynchronous
import and transport supervision. Its separate worker owns Qt Multimedia and decoding.
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

Native version 4 adds explicit source-clock conventions and duration estimates. Versions
1–3 are explicitly migrated at load while preserving legacy per-stream semantics; unknown versions fail. No history is invented for imported
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

A validated preview plan is a detached, revision-labelled copy of one populated track.
The Qt transport sends a source path/range to a separate worker over a private local socket.
The worker uses Qt Multimedia's FFmpeg backend with software decoding, emits default-device
audio, and returns timestamped preview images and position observations. Cancel or seek
invalidates old output and kills the worker; rapid seeks coalesce to the latest request.
Local socket transfer limits, load deadlines and a playback watchdog bound failures.
The worker is process isolation, not a security sandbox.

Core edit times remain exact fractions. A separate signed SourceTime models original packet
origins. Shared sources retain relative stream starts and the union of their spans; legacy
assets keep the earlier convention. Verified relink preserves the asset's clock mode.

A bounded background ffprobe job builds a decoded-frame index. Paused seeks choose a held
frame from presentation timestamps and verify its decoded PTS in the Qt worker. Frame-step
controls change only the playhead. The index/cache is invalidated by source identity/metadata
or modification-time changes and is not persisted. Negative origins use a temporary verified
packet-copy source because the measured Qt backend cannot seek negative packets directly.
Cancellation coalesces both frame preparation and playback work.

Preview includes cuts and blank/silent gaps, with loading pauses at cuts. It does not mix
separate tracks or guarantee sample-accurate audio cuts. See [desktop preview](desktop-preview.md),
[evaluation](precision-evaluation.md) and [ADR 0011](adr/0011-source-origins-and-frame-index.md).

## Local agent adapter

src/mcp owns JSON wire conversion, schema discovery, stdio lifecycle and a bounded session
registry for proposals and retry results. Its launcher owns the project path, fixed actor,
permissions and cooperative save lock. JSON/MCP do not enter nle_core. Agent edits use the
existing Editor/Transaction API and preserve version-4 native files and exact source clocks.
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
decoder/render determinism, seamless playback and advanced timeline interaction remain outside
this implementation. MCP has process-local retry protection; desktop/CLI/MCP writes share
cooperative file ownership, and recovery restores checkpoints without restoring sessions.
