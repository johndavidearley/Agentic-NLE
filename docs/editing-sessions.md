# Editing sessions

Milestone 2 introduces atomic multi-edit batches, optimistic revision checks, durable
attribution, pull-based notifications, and bounded session-local history.

## API example

~~~cpp
nle::Editor editor(nle::load_project("project.nle"));
const auto before = editor.snapshot();
auto batch = editor.begin({
    {nle::ActorId{"agent:assistant"}, nle::ActorKind::Agent},
    "Trim the opening",
    before.revision
});
batch.execute(nle::TrimClip{clip_id, {0}, {{2}, {8}}});
batch.execute(nle::SplitClip{clip_id, {4}});
auto preview = batch.preview();       // Detached candidate; live state is untouched.
auto operation = editor.commit(std::move(batch)); // One revision, one undo item.
auto changes = editor.changes_since(before.revision);
editor.undo();                       // New revision, referencing the original operation.
nle::save_project(editor.snapshot(), "project.nle");
~~~

All IDs in the example must belong to the loaded project. Actor IDs are caller-supplied
identifiers, not authentication credentials.

## Transactions

A transaction is move-only and tied to its originating live Editor by a weak identity
token. Another Editor cannot commit it even if it loaded an identical project.
Destroying the owner invalidates its transactions. Transactions themselves are
single-owner objects; callers must not concurrently operate on one transaction.

Each staged command uses the same application/validation function as direct editing.
A failed command invalidates the entire batch. Commit, failed commit, explicit rollback,
or destruction closes/discards the batch. Closed or moved-from transactions cannot be
used. Rollback is idempotent. No staging operation mutates the live project's state,
revision, history or attribution. There are at most 1,024 commands per batch, including no-ops.

Preview IDs are provisional and scoped to that transaction. Competing candidates can
allocate the same tentative IDs; revision checks allow at most one to commit.
Do not publish tentative IDs as durable references. IDs from abandoned batches may be
reused; committed IDs are never reused across undo and save/load.

A batch with identical final state is a no-op and preserves redo. A create-then-delete
batch still advances the committed allocation watermark and is recorded.
Every intermediate staged state must be valid; overlapping intermediate clips are
rejected even if a later proposed command would resolve the overlap.

## Revisions and concurrency

Every successful non-no-op edit, batch, undo, or redo increments the persisted revision.
RevisionConflict distinguishes stale requests from other DomainErrors.
A stale commit closes its transaction without changing the live state.
Undo/redo never rewind revisions, preventing an old candidate from becoming current again.

Direct execute/undo/redo and begin accept RequestContext.expected_revision.
Omitting it requests an edit against the current state and is intended for local
synchronous callers. External adapters should require it. Snapshots include their revision.
An Editor mutex serializes its entry points; two writers with the same expected revision
cannot both succeed. There are no callbacks under this mutex.

This synchronizes one Editor, not multiple processes or independently loaded Editors.
Use one authoritative editing session per project file. Desktop, mutating CLI operations and
writable MCP sessions share DocumentFile ownership and expected-file checks outside Editor.
[Recovery checkpoints](project-recovery.md) restore state after ordinary interruption without
restoring session history. MCP retry records remain process-local. Atomic compare-and-swap
saves with noncooperating writers, remote authentication and durable retry replay remain future work.

## Attribution and change notifications

Project snapshots persist OperationRecords: operation ID, revision, actor ID/kind, label,
change kind, original edit target for undo/redo, and ordered command summaries.
Operation IDs are project-scoped and equal the revision of their creation.
Undo/redo append records; they do not erase or replay attribution.

changes_since(revision) returns detached records after that cursor; current revision
returns an empty list and a future cursor raises RevisionConflict. This pull-based
notification surface needs no UI, protocol or event-loop dependency.
No-ops, failed edits and rollbacks create no records.

Command summaries identify operation types, affected IDs and key timing parameters.
They are descriptive metadata, not an executable event journal, a complete parameter
record, or cryptographic proof of who performed an action. Labels/actors are supplied
by trusted callers. File integrity and authorization belong to future adapters.

## History budgets and persistence

Default history limits are 100 entries and 64 MiB of accounted snapshot payload shared
between undo and redo. Oldest undo entries are evicted on a new edit until both limits
fit. Undo/redo transfer entries without duplicating them. A single edit larger than
the budget is rejected atomically; a batch failing this check is closed.

Setting either limit to zero explicitly disables history. A successful unrecorded edit
clears history so an older undo cannot skip across it. Attribution remains enabled.
history_usage reports retained entry counts and payload bytes; it excludes allocator
overhead, shared_ptr bookkeeping, live state and temporary candidate copies. It is
not a process-memory ceiling. String SSO may be overcounted.

The durable operation log is separate from undo history and is capped at 10,000 records.
At capacity, further state-changing operations fail without modifying state. No silent
audit truncation is performed. The native file also retains its 16 MiB/100,000-record
limits, which can be reached earlier. Audit compaction needs a future schema decision.

Only project state, watermark, revision and attribution are saved. Open transactions,
undo/redo stacks, history limits and the session identity token are not persisted.
Version 1 files migrate to revision zero with no invented historical attribution.

## Additional commands

DeleteTrack removes a track and its clips; media assets remain. Undo restores clip IDs,
source ranges and the original track position. ReorderTrack moves an existing track
to a final zero-based index within its sequence. It cannot move tracks across sequences.
RelinkMedia sets/removes an original or proxy locator without changing media identity
or clip references; it does not probe or verify files. All support transactions and undo/redo.
