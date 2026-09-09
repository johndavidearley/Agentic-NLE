# Architecture

Status: implemented for Milestone 1 unless marked future.

~~~text
Future Qt UI ----+
CLI ------------+--> Editor::execute(Command) --> private project state
Future MCP -----+            |
                        validate candidate
                        commit + history
                             |
                      detached snapshot()
                             |
                       native persistence
~~~

## Ownership

src/core owns exact time and domain errors. src/project defines value DTOs, validation,
and persistence. src/commands owns the editing session. src/cli is a domain client.
There are no UI or protocol dependencies.

Editor owns state and undo/redo history. Commands form a closed typed variant.
They resolve IDs in a candidate copy, modify it, sort clips, validate the result,
allocate history, then commit by move. Failure leaves state, the ID watermark, and
both histories unchanged. No-ops preserve history. Temporary internal lookup pointers
are neither ownership nor persistent identity.

Construction from a snapshot validates it. Snapshots are deep detached copies;
changing a DTO does not change its originating editor.

## Identity and order

Project IDs are nonzero random 64-bit values. Sequence, track, clip, and asset IDs
are distinct C++ types holding nonzero project-local integers. External references
must include project ID. A monotonic watermark allocates IDs across object types,
independent of filenames or vector positions.

Undo never rewinds that watermark. Redo restores original IDs. Save preserves the
watermark even for undone creations; failed commands publish no identity.
Cross-project duplication/merging needs a later UUID policy. Project IDs are probabilistic,
not a collision-proof global namespace.

Sequences, tracks, assets and locations preserve insertion order. Clips sort by exact
position then ID. Duplicate identities and noncanonical clip ordering are rejected.

## Time and semantics

RationalTime stores reduced nonnegative seconds with a positive denominator.
A 24000/1001 fps frame is 1001/24000; a 48 kHz sample is 1/48000.
The sequence stores frame duration separately, preserving its intended grid.
Positions are not automatically snapped. Comparison uses continued fractions;
arithmetic checks overflow. Signed offsets, timecode, snapping and speed are deferred.

Ranges are half-open. Clips need positive duration, valid source bounds, compatible
media/track kinds and no same-track overlap. At speed 1 source duration equals timeline duration.

- Insert references an existing logical asset.
- Move sets position and destination track, potentially across sequences.
- Trim explicitly sets position and source range, allowing valid handle extension.
- Split takes an absolute interior timeline position; left ID remains, right ID is new.
- Delete removes the clip without shifting neighbors.

## Media and persistence

A logical asset owns identity, name, declared stream capability and duration.
Optional original/proxy locators are not identity. Empty locations mean offline media.
Registration does not probe files. Generated media, relinking and decoding are deferred.

The native model belongs to this application. Persistence consumes detached snapshots.
Save validates before disk writes, stages beside the target and replaces the destination.
History is session-local. See [native format](native-format.md).

## History, transactions and scale

History stores before/after snapshots; undo/redo preserve the highest allocation watermark.
Real new edits clear redo. This costs O(project size) per history entry; full validation
also adds work. Validation indexes media references; other storage is deliberately simple.

Future grouped transactions can own a candidate session, apply current commands,
preview a snapshot, and commit one history entry after checking a base revision.
Rollback discards the candidate. Revisions, preview ID allocation, history limits,
attribution and serialized concurrent access must exist before exposing agent transactions.

No transaction API, thread safety, durable journal, or rendering guarantee is claimed.
See [MCP boundary](mcp-boundary.md).
