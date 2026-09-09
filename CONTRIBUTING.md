# Contributing

Keep changes small and tied to a current editing requirement. Build and run CTest
before proposing changes; include relevant invariant and failure-path tests.
Use clang-format 19 and keep compiler warnings clean. Tests use explicit checks
that remain active in Release builds.

Route mutations through Editor::execute. Keep live state private and return detached
values for inspection. IDs must be typed, persistent, and project-scoped.
Do not add Qt, codec, MCP, or filesystem concerns to timeline command logic.

Document semantic changes and substantial dependencies in an ADR. Record the
problem, alternatives, license, status, and consequences. Avoid placeholder subsystems.

Persistence changes require round-trip and malformed-input tests, a format document
update, and an explicit compatibility decision. Never silently reinterpret a version.

Describe the problem, behavior, and validation in pull requests. Use descriptive
commits and do not rewrite published history. Contributions use the MIT license.
