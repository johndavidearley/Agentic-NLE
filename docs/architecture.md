# Architecture

Implemented through Milestone 2 — Hardened Editing Sessions.

~~~text
Future Qt UI ----+
CLI ------------+--> Editor / Transaction --> shared command application --> candidate
Future MCP -----+          |                          |
                     expected revision         validate invariants
                           |                          |
                     atomic commit + bounded undo history
                           |
                     detached snapshot + durable operation records
                           |
                     native version 2 persistence
~~~

## Boundaries

src/core owns exact rational time and domain errors. src/project owns detached project
DTOs, validation, attribution records and persistence. src/commands owns sessions,
transactions and typed commands. src/cli proves both milestones without Qt or decoding.
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
Signed offsets, snapping, timecode and speed changes remain deferred.

Clips use positive-duration half-open ranges at speed 1. Source bounds and media/track
compatibility must hold, and same-track overlaps are rejected. Moves may cross sequences.
Trims explicitly specify position and source range. Split preserves the left ID and
allocates a right ID. DeleteClip does not ripple; DeleteTrack cascades clips but retains media.

## Media, provenance and persistence

A logical asset owns ID, name, declared stream capability and duration. Original/proxy
locators are replaceable. Empty locations represent offline media. Relinking is metadata
editing, not probing or decoding.

Native version 2 adds revision/attribution to the application-owned model. Version 1 is
explicitly migrated at load; unknown versions fail. No history is invented for imported
old projects. Attribution records survive undo and save/load, but are not an authenticated
or replayable event journal. Persistence consumes snapshots and atomically replaces a
fully staged file during ordinary operation; power-loss durability is not promised.
See [native format](native-format.md).

## Scaling and remaining boundaries

Snapshot history remains O(project size) per retained edit. Default retention is 100 entries
and 64 MiB of accounted payload. A single oversized history item is rejected, and callers
can explicitly disable history. Each transaction stages at most 1,024 commands.
The audit cap is 10,000 durable operations. There is no silent loss of audit metadata.

The [benchmark](performance.md) measures 100-command transactions on 1,000/10,000 clips.
This validates a bounded milestone workload, not professional-scale performance.
Independent processes, distributed merges, authentication, idempotency, decoder/render
determinism and UI integration remain outside this implementation.
