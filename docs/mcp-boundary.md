# MCP boundary (design only)

MCP translates wire requests into domain commands and returns detached snapshots.
It does not drive the GUI or become the internal command bus.
The [official MCP architecture](https://modelcontextprotocol.io/specification/2025-11-25/architecture/index)
informs protocol concerns; SDK, transport and version negotiation remain outside the core.

| Proposed capability | Domain mapping |
| --- | --- |
| project.get / timeline.inspect | Editor::snapshot, optionally filtered |
| project.save | Snapshot plus authorized persistence |
| timeline.insert / move / trim / split / delete | Existing typed commands |
| media.list / media.get | Snapshot media library |
| media.import | Future metadata probe then RegisterMedia |
| transaction.begin / preview / commit / rollback | Future candidate session and revision check |

Wire IDs should use decimal strings plus kind and project ID to avoid JavaScript
integer precision loss. The adapter converts to strong types. Rational value/rate
fields should also preserve integer precision. Never implicitly round floating seconds.

Read-only resources can use URIs such as nle://project/{project_id}/sequence/{sequence_id}.
Return snapshot DTOs and a future revision token, never mutable state.

Before enabling writes, add serialized session access, expected-revision checks,
idempotency keys, operation IDs and actor attribution. Transactions preview candidates
and commit one undoable item; rollback publishes nothing. Define preview ID allocation
and stale-base behavior before implementation. None of these future guarantees is
claimed by the bootstrap.

Map errors into distinguishable malformed-argument, invalid-edit, stale-revision,
permission and internal failures. Do not expose stack traces or unrestricted filesystem
access. Remote access requires an explicit authentication and authorization design.
Protocol tests must prove the same commands have the same state/history effects as UI
and CLI clients. Protocol testing supplements core invariant tests.
