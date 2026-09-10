# MCP boundary

Protocol adapter design only; the headless session primitives now exist.

MCP will translate wire requests into the same typed commands and transactions used by
the human UI and CLI. SDK, transport and protocol negotiation remain outside nle_core.
No MCP server or GUI automation is implemented.

| Proposed capability | Domain mapping |
| --- | --- |
| project.get / timeline.inspect | Editor::snapshot, including revision |
| project.save | Snapshot plus authorized persistence |
| timeline.insert / move / trim / split / delete | Existing commands with expected_revision |
| timeline.track.delete / reorder | DeleteTrack / ReorderTrack |
| media.list / get | Detached snapshot media |
| media.import | Future metadata probe, then RegisterMedia |
| media.relink | RelinkMedia |
| transaction.begin / preview / commit / rollback | Editor::begin, Transaction::preview, Editor::commit, Transaction::rollback |
| change notification | Editor::changes_since(cursor) |

The adapter must supply actor context and expected revision for external writes.
RevisionConflict identifies stale requests. The Editor serializes its own entry points;
transactions have single-owner lifetimes and must be stored in an adapter-owned session
registry. Failed or stale batches close without publishing partial state.

Wire IDs should use decimal strings, object kind and project ID to preserve integer
precision. Rational times must retain exact numerator/denominator values.
Preview IDs are provisional and must never become shared durable references until commit.

Resources such as nle://project/{project_id}/sequence/{sequence_id} return detached state
and revision. Poll changes_since or translate its records into protocol notifications.
Attribution is caller-supplied metadata; authentication must determine who may claim an actor.

Before exposing a server, implement transport authentication/authorization, filesystem
scoping, request idempotency, session expiry and structured error mapping. File writes
remain single-authority; independent process saves need conflict handling.
Protocol tests must prove the same edit requests have the same state and history effects
as local callers. Never expose stack traces or arbitrary filesystem access as tool output.

The [official MCP architecture](https://modelcontextprotocol.io/specification/2025-11-25/architecture/index)
informs protocol boundaries; it does not define the internal project model.
