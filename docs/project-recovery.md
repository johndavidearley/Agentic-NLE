# Project saves and recovery

Milestone 7 gives desktop, CLI and MCP writes the same project ownership and save checks.
A second writer receives an error while another editor owns that path. Read-only inspection
still works. Saving checks for external changes before replacing a file; if another program
changed it, keep the current work open and use Save As to a different path.

Save As preserves the project identity and makes a saved copy. Independently edited copies
are not automatically merged. Original media is not copied or changed by project saving.

## Desktop

Dirty named projects get a recovery checkpoint every 30 seconds beside the saved file.
Untitled projects use the application-local recovery folder supplied by QStandardPaths on
each operating system. Recovery is attempted on the desktop event loop; slow/busy operations
can delay a checkpoint. The status area reports checkpoint failures. Save explicitly when
finishing a significant edit; changes since the last completed checkpoint can still be lost.

Opening a project with a matching checkpoint offers Recover, Discard recovery or Cancel.
If the newest checkpoint is damaged, a valid older one is offered with a warning. A recovered
project is marked unsaved and needs Save or Save As. Missing media does not prevent recovery
of the timeline; it remains offline until relinked. Checkpoints for a different saved version
are not applied automatically. The saved baseline must remain readable and match exactly;
this workflow does not repair a separately corrupted or externally replaced saved project.

An available untitled draft is offered at startup. Recover draft also checks for interrupted
untitled work; drafts still owned by another running editor are skipped. Closing/discarding
normally removes that document's checkpoints. Successful save removes them too. Abnormal
termination leaves the last completed checkpoint available. The saved file is never silently
replaced with a recovered version.

## CLI

~~~sh
editor-cli recovery-inspect project.nle
editor-cli recover project.nle
editor-cli discard-recovery project.nle
~~~

recovery-inspect only reads the best matching checkpoint. recover explicitly replaces the
saved project with it; discard-recovery explicitly removes both checkpoint slots. Mutating
CLI commands also hold the shared writer lock, including media import while probing occurs.
They reject unreviewed recovery files instead of silently erasing interrupted work.

## MCP

The existing server remains read-only by default, with separate --allow-edit and --allow-save
permissions. Add --recovery alongside --allow-edit to permit checkpoint writes, even when
ordinary project-save permission is absent:

~~~sh
editor-mcp --project project.nle --allow-edit --allow-save --recovery
~~~

When prior recovery files exist, select one explicit startup action:

~~~sh
editor-mcp --project project.nle --allow-edit --allow-save --recovery --recover
editor-mcp --project project.nle --allow-edit --allow-save --recovery --discard-recovery
~~~

Successful commit/undo/redo attempts a checkpoint before replying. project_get reports
recovery_enabled, recovery_revision (last completed checkpoint for unsaved work in this
process; cleared by save) and
recovery_error. A nonempty recovery_error after an edit means the edit succeeded but its
checkpoint failed; save explicitly. Repeating the same request key still replays the original
result, including that warning. Save failures leave in-memory work and prior recovery available.

Without --recovery, no checkpoints are created and closing stdin still discards unsaved
in-memory changes. A save cannot silently replace unreviewed recovery files left by another
session. With --recovery, EOF, session expiry or process exit leaves completed checkpoints.
A recovered process has fresh session/retry/proposal IDs and empty undo history. Inspect the
recovered revision before deciding whether an uncertain old edit needs doing; do not replay
old requests into a new session assuming exactly-once recovery.

## Files and limits

Each project may have .mcp-lock, .recovery-0 and .recovery-1 sidecars. The lock name is retained
for compatibility; it now coordinates all three application entry points. Empty lock files
remain on disk for safe reuse and do not mean a process is still running. Do not remove active
lock files to force a second writer. Sidecars are excluded from Git.

Each checkpoint stores at most two 16 MiB native documents plus its small envelope; two slots
retain the newest and prior valid checkpoints. Untitled drafts are separate documents, so this
is a per-document limit. There is no silent deletion policy for older user drafts. Recovery
checks validate syntax, identity, revision and exact saved baseline, not cryptographic integrity.

The timeline command contract and native version 4 are unchanged. Recovery never restores undo stacks, open
transactions, replayable commands or the agent's conversation. This is protection against
ordinary process interruption, with a bounded checkpoint window. It does not promise
power-loss durability or atomic conflict detection against noncooperating programs.

See [ADR 0013](adr/0013-document-ownership-and-recovery.md) for the file contract and
[Milestone 7 validation](recovery-evaluation.md) for executed checks and platform evidence.
