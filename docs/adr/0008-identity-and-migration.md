# ADR 0008: Single-authority identity and explicit native migration

Status: accepted for milestone 2. Date: 2026-09-10. Extends ADR 0003.

Retain nonzero random 64-bit ProjectIds and monotonic typed project-local object IDs.
Use project-scoped OperationIds equal to their persisted creation revision.
This milestone supports one authoritative Editor per project file, not distributed merging.

UUIDs would reduce cross-project collision risk but do not solve conflicting edits or
copied-project identity by themselves. A UUID conversion now would change every native
project identity without an implemented cross-project workflow. Do not add a UUID library
or pretend existing 64-bit values are UUIDs. A future cross-project/import/merge feature
must introduce a versioned UUID policy and explicit identity remapping first.

A filesystem copy represents the same project, not a new project identity.
Independently editing copies can fork revisions and is outside the merge/conflict guarantees.
External consumers always qualify IDs with ProjectId. IDs are not authorization tokens.

Preview IDs are transaction-scoped and provisional. Rolled-back candidates may reuse them;
committed allocations persist through undo, deletion and reload without reuse.
Monotonic operation revisions survive undo and reload, preventing stale-session ABA.

Version 2 adds an explicit audit block. Version 1 is migrated to revision zero and empty
attribution while preserving all domain IDs and next_id. Writers emit version 2 only;
unknown versions fail and there is no implicit downgrade or inferred missing-field migration.
Future schema changes require fixtures, migration tests and a compatibility decision.

This policy makes current identity guarantees explicit without claiming global collision
proofing, multi-writer synchronization or historical attribution for legacy files.
