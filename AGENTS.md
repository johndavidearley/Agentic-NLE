# Repository instructions

This is an independent project. Do not copy source or architecture from unrelated repositories.

- Bound milestone work by docs/roadmap.md and the user's current request.
- The core is C++20, headless, and independent of Qt, MCP, and media backends.
- Route edits through commands. Inspect via detached snapshots.
- Preserve typed IDs, exact rational time, validation, and undo/redo behavior.
- New dependencies need an ADR including license and alternatives.
- Run relevant CTest suites with warnings as errors. Format with clang-format 19.
- Keep APIs small; avoid speculative modules, singletons, and GUI automation.
- Make logical commits when requested; do not rewrite published history.
