# ADR 0013: Shared document ownership and recovery checkpoints

Status: accepted for Milestone 7, 2026-09-16. Extends ADRs 0008 and 0012.

## Decision

Desktop, mutating CLI operations and writable MCP sessions use DocumentFile, a small
standard-library/OS file service alongside native persistence. It owns a canonical project
path, the exact loaded/last-saved bytes and a cooperative exclusive file lock. The existing
.mcp-lock name is retained so Milestone 6 saving MCP servers participate in the same lock.
Readers may inspect saved files without owning the lock. Raw serialization/persistence stays
available for fixtures and other adapters; it is not itself a multi-writer document service.

All application saves compare the current file with the expected bytes before same-directory
atomic replacement. New-file saves reject a destination that appeared after opening. Saving
a copy preserves the project's existing identity, revisions and allocation watermark. It does
not create an independently mergeable branch or invent a new identity policy. A failed save
preserves the live Editor and previous saved state. Noncooperating programs and hard-link
aliases are outside the lock's coordination guarantees; comparison/replacement is not a
filesystem compare-and-swap operation.

Recovery uses two adjacent files, .recovery-0 and .recovery-1. Each envelope contains the exact
baseline saved bytes plus a complete validated native snapshot. The highest valid revision
matching that baseline is offered; a damaged newest checkpoint falls back to the other slot.
Updates replace the older/invalid slot and preserve the newest valid prior checkpoint. The
16 MiB native limit applies to each embedded document; each slot is at most 32 MiB plus a small
header. This bounds disk payload per document, not total process memory or all user drafts.

A checkpoint never replaces the saved project. Recovery requires an explicit recover/discard
choice, including before a new client can save over unreviewed recovery files. Recovery
restores project content, IDs, revision and audit metadata; Editor history, proposals, session
IDs and retry records start fresh. Successful save retires checkpoints. A cleanup error after
a completed save is a warning, not a failed write that could invite an unsafe retry.

The desktop attempts a checkpoint every 30 seconds while dirty. Named documents use adjacent
sidecars; untitled drafts use the application-local recovery directory. Drafts are offered at
startup or through Recover draft. Active drafts remain locked and are skipped. Explicit user
discard removes recovery data; abnormal termination preserves the last completed checkpoint.
MCP enables checkpoint writes only through --recovery plus --allow-edit. Each successful commit,
undo or redo attempts a checkpoint before replying. Checkpoint failure is reported separately
from the already successful edit. --recover/--discard-recovery resolve a previous checkpoint.
Save permission remains separate, and no recovery file is written in the default MCP mode.

## Format and compatibility

Envelope header: NLE_RECOVERY 1 BASE_BYTES SNAPSHOT_BYTES followed by a newline, exactly the
specified baseline bytes, then exactly the specified native snapshot bytes. A zero-length
baseline denotes an untitled document. Native validation, bounded sizes, exact baseline match
and project identity/revision checks reject incompatible records. This envelope is a recovery
artifact, not the native project format; native version 4 is unchanged.

The current slot is staged and atomically replaced using the existing persistence primitive.
This addresses ordinary process interruption, not power loss, hostile filesystem races,
cryptographic integrity or acknowledged edit durability between checkpoints. The operation
log remains descriptive and is not replayed as an event journal.

## Alternatives and dependencies

No new third-party dependency is introduced. Reusing MCP-only protection would leave desktop
and CLI writes uncoordinated. A single autosave file would lose the fallback when that file is
damaged. An executable command journal would require a new persistent replay and schema
compatibility contract. Full filesystem transactions or distributed ownership exceed the local
single-writer milestone. Two validated snapshots reuse the existing migration/validation model.
