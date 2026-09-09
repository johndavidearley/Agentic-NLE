# ADR 0004: Typed commands and snapshot history

Status: accepted; transactions deferred. Date: 2026-09-09.

Editor accepts typed command variants. Each edits a candidate, validates invariants,
and commits a before/after history entry. Snapshots leave by value. UI/protocol callers
cannot mutate the live model.

This provides atomic failure and undo/redo for all mutations, including creation and
media registration. Committed ID watermarks survive undo and persistence.

Inverse commands reduce memory but complicate restoration of IDs, ordering and deleted
objects. Event sourcing introduces replay/versioning work before it is needed.
Snapshot history is an initial implementation, not a performance claim.

Transactions can later preview a candidate and commit one entry after revision validation.
Current APIs provide only single-command atomicity. Measure memory/latency, limit history,
and add revision and attribution metadata before large projects or agent batches.
