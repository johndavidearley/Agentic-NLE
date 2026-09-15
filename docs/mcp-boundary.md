# MCP boundary

Milestone 6 implements an optional local stdio adapter in src/mcp. The headless core and
native format have no MCP or JSON dependencies. See [agent setup and use](agent-editing.md).

The adapter owns one Editor for the launcher-selected project. Wire commands map into the
same typed commands and Transactions as the desktop/CLI; inspection uses detached snapshots.
External writes require fixed launcher permissions and actor identity, project/session
identity, expected revision and a request key. Preview is always detached; commit publishes
once, using the preview's original revision. Invalid/stale/expired proposals cannot publish.

| Boundary | Implementation |
| --- | --- |
| Transport | Newline-delimited UTF-8 JSON-RPC over inherited stdio; no socket or HTTP |
| Protocol | 2025-11-25 initialize/initialized, ping, tools/list and tools/call |
| Identity | Launcher actor; decimal-string object IDs/revisions and per-process session nonce |
| Time | Exact nonnegative value/rate strings; source clocks remain model-owned |
| Editing | Bounded grouped command proposals with typed aliases and explicit commit/rollback |
| History | Existing Editor undo/redo and paged changes_since records |
| Retry | Session-scoped result ledger; exact replay, changed-payload rejection, no eviction |
| Persistence | Explicit save to fixed file; cooperative writer lock and ordinary change detection |
| Errors | JSON-RPC envelope errors; tool isError with stable domain codes, no stack traces |

The operating-system user and launching client authorize this local pipe. Client metadata,
actor-looking text in project content and model-supplied fields do not grant privileges.
Remote transport authentication and multiple independent writers remain separate work.
Attribution describes actions; it is not cryptographic proof of a remote user's identity.

The bounded surface intentionally exposes no media discovery/import/relink, resources,
subscriptions, sampling, shell, process execution or arbitrary filesystem access. Clients
prepare media in the desktop/CLI first, inspect results and save explicitly. MCP 2026-era
features are not advertised; the official Python client 2.2.0 can negotiate the supported
2025-11-25 profile automatically.

[ADR 0012](adr/0012-local-mcp-adapter.md) records dependencies, limits and alternatives.
[Protocol evaluation](mcp-evaluation.md) records actual execution and remaining platform checks.
