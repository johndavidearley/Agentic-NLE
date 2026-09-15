# ADR 0012: Local MCP editing adapter

Status: accepted for Milestone 6, 2026-09-14.

## Decision

Add an optional C++ stdio MCP executable beside the CLI. It owns one Editor loaded from a
launcher-selected native project. MCP protocol 2025-11-25 uses newline-delimited JSON-RPC,
initialization/capability negotiation and tools discovery/calls. No socket, HTTP service,
remote authentication, media probing or GUI automation is included. The launching client
and operating-system user establish the local trust boundary. Actor identity and edit/save
permissions come from launcher arguments, never from tool arguments or clientInfo.

Expose read-only project snapshots and paged audit changes; grouped edit preview followed
by explicit commit/rollback; undo/redo; and explicit save to the configured project only.
Preview commands operate through Transaction and committed commands through Editor. Stable
IDs/revisions use decimal strings; times use exact value/rate string pairs. Batch aliases
reference newly allocated objects without guessing IDs. Preview IDs remain provisional.

A session nonce, project ID, expected revision and request key scope mutations. Exact retries
return the recorded result; a reused key with changed arguments rejects. Retry records are
bounded and retained for the session rather than evicted. Capacity is reserved
for one final save attempt after the normal request budget fills. Expired sessions reject
further tool operations, including save.
Proposals have a shorter lifetime and close on failed/stale commit. All state/history stays
session-local until explicit save; closing stdin discards unsaved state. Save permission is
separate from editing. A cooperative sidecar file lock excludes a second saving MCP process;
comparison with the loaded/saved bytes detects ordinary external changes before save. This
is not atomic coordination with a noncooperating desktop/editor; use one writer per file.

## Dependency and alternatives

The adapter alone uses nlohmann/json 3.12.0 (MIT) for JSON parsing, serialization and schema
construction. CMake accepts an installed package or downloads the pinned upstream archive
with SHA-256 verification when NLE_BUILD_MCP is explicitly enabled. Default/headless core
builds remain dependency-free. No third-party source or binary is committed.
The optional interoperability test uses the official Python MCP SDK 2.2.0 (MIT), installed
only in a test environment; it is not part of the server runtime. Its automatic negotiation
falls back to this adapter's 2025-11-25 handshake profile.

Qt JSON would couple this headless adapter to the desktop runtime; a bespoke JSON parser
would add unnecessary correctness/security work. A separate Python server would need an
additional stateful bridge into the C++ Editor. A narrow implementation of the standard
stdio/tools surface keeps protocol code outside the core. Full MCP SDK/server features can
be reconsidered when remote/multi-client use is explicitly scoped.

References: [MCP stdio](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports),
[lifecycle](https://modelcontextprotocol.io/specification/2025-11-25/basic/lifecycle),
[tools](https://modelcontextprotocol.io/specification/2025-11-25/server/tools),
[nlohmann/json](https://github.com/nlohmann/json/releases/tag/v3.12.0).
