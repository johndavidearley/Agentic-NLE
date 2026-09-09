# ADR 0006: MCP as external adapter

Status: boundary accepted; SDK/transport deferred. Date: 2026-09-09.

All clients use Editor commands and snapshots. MCP translates typed wire requests
into operations and exposes read-only resources. Protocol sessions and capability
negotiation stay outside the domain.

A protocol-shaped engine couples every client to transport/version choices.
GUI automation cannot provide the required persistent identity and editing semantics.
Both alternatives are rejected.

No SDK is needed yet. Review maintained SDKs, licenses and protocol support at the
integration milestone; keep their headers outside the public core.

See [MCP boundary](../mcp-boundary.md) for mappings and prerequisites.
The [official protocol architecture](https://modelcontextprotocol.io/specification/2025-11-25/architecture/index)
informs the adapter, not the native project model.
