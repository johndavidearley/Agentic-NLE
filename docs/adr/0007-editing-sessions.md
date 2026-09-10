# ADR 0007: Candidate transactions, durable revisions and bounded history

Status: accepted for milestone 2. Date: 2026-09-10. Extends ADR 0004.

Use move-only candidate transactions, session ownership tokens and persisted optimistic
revision checks. Failed staged commands poison the entire batch. Commit validates the
base revision and publishes one state/history/attribution transition.
The same command application function handles direct and staged editing.

A mutex protects Editor entry points. A Transaction remains single-owner to keep its
lifetime and staging simple. Multiple independently loaded Editors are not coordinated.
An adapter must maintain one authoritative session per project.

Persist attribution as ordered operation records, including actor ID/kind, label, edit
summaries and undo/redo targets. IDs equal the operation revision in a separate typed
namespace. Pull-based changes_since avoids callbacks, reentrancy and UI/protocol
dependencies. This is metadata, not event sourcing, authentication or command replay.

History retains immutable before/after content through shared entries. Audit logs are
excluded from history snapshots. Default limits are 100 entries/64 MiB accounted payload.
Oldest entries are evicted; an oversized single entry fails atomically. Zero disables
history explicitly. Live/candidate allocations and allocator overhead are not a hard RSS cap.

Alternatives: inverse-command history saves memory but is harder to prove; a shared
mutable candidate risks partial publication; holding a lock for a whole agent preview
blocks human edits; durable event sourcing adds replay/schema concerns. None is needed
for this bounded workload. Snapshot cost remains measurable and explicitly limited.

Cap batches at 1,024 commands and audit logs at 10,000 records. Do not silently truncate
attribution. Audit archival/compaction and retry idempotency need later designs.
