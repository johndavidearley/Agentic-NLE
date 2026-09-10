# Native project format, version 2

The writer emits version 2. The reader supports versions 1 and 2 and rejects all others.
Version 1 is documented in [native-format-v1.md](native-format-v1.md).

Version 2 preserves the complete version 1 PROJECT/MEDIA/SEQUENCES grammar, changes
the header to NLE_PROJECT 2, and inserts an AUDIT block before END:

~~~text
NLE_PROJECT 2
PROJECT project_id next_id "name"
MEDIA ...
SEQUENCES ...
AUDIT revision N
  N * OP operation_id revision kind target_operation_id actor_kind "actor_id" "label" A
      A * ACTION "command summary"
END
~~~

N and A are exact record counts. Operation kind: edit=0, undo=1, redo=2.
Actor kind: human=0, agent=1, system=2.
Actor IDs, labels and action strings use the existing quoted-text grammar.
They are nonempty, at most 4096 bytes, and exclude ASCII controls and DEL.

Operation IDs/revisions are contiguous starting at 1 in a namespace separate from
timeline object IDs. Project revision equals operation count. For an edit, target is
zero and there are 1–1,024 actions. Undo/redo have no action strings and target an
earlier edit operation. The reader validates all these constraints, known enums,
and all version 1 timeline/identity invariants.

The total record budget is 100,000 assets, locations, sequences, tracks, clips,
operations and actions combined. File size remains 16 MiB. The operation log is also
limited to 10,000 entries. Saves enforce reader limits before changing an existing file.

## Migration and identity

Version 1 loads with revision zero and an empty operation list while preserving the
project ID, all object IDs, object ordering, locators, time values and next_id watermark.
No attribution is invented. The next real edit gets operation ID/revision 1.
The next save emits version 2. There is no version 2 to version 1 downgrade writer.

Unknown versions, missing audit blocks, malformed records and trailing non-whitespace
fail cleanly. Mutation fuzz tests accept only rejected input or a fully validated
round-trippable document. Migration is explicit in the reader, not guessed from missing fields.

Operation attribution persists but is neither a replay journal nor proof of authenticity.
Command summaries are not interpreted as executable commands during load.
Open transactions, undo/redo stacks, budgets and mutex/session identity remain session-local.

## Save semantics

The existing adjacent staging-directory and replace strategy remains. All validation
and serialization happen before destination replacement. Tests cover invalid data,
directory targets, and Windows sharing locks, verifying that replacement failure preserves
the old file and cleans up staging artifacts.

This does not provide independent-writer conflict detection, power-loss fsync guarantees,
crash recovery, backup rotation, or a distributed merge protocol.
