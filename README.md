# Agentic NLE

A professional open-source video editor designed for human editors and software agents.
This repository implements **Milestone 2 — Hardened Editing Sessions**.
There is no graphical editor, decoder, playback engine, or MCP server yet.

The C++20 core provides typed persistent identities, exact rational time, detached
inspection snapshots, validated commands, grouped transactions, revisions, actor/operation attribution, bounded undo/redo, and versioned native persistence.
Human UI, CLI, and future MCP adapters share the same command boundary.

## Build and test

Requires CMake 3.24+ and a C++20 compiler. No third-party libraries or downloads
are required. Locally verified with MSVC 19.44 on Windows x64; CI targets GCC/Clang on Linux and Apple Clang on macOS (Apple Silicon and Intel), in Debug and Release.

PowerShell with Visual Studio 2022 C++ Build Tools installed:

~~~powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
~~~

Linux and macOS (CMake plus a C++20 toolchain; on macOS, install Xcode Command Line Tools):

~~~sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
~~~

For a Release build on Linux/macOS, use a separate directory:

~~~sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
~~~

macOS CI uses the explicit macos-15 (Apple Silicon) and macos-15-intel runners.
macOS execution must be verified by CI; it has not been run from this Windows workspace.

Warnings are errors. Formatting uses clang-format 19. Set
-DCLANG_FORMAT=/path/to/clang-format when configuring if it is not on PATH,
then build targets **format** or **format-check**.

## Run the vertical slice

~~~powershell
.\build\Debug\editor-cli.exe session-demo .\build\session-demo.nle
.\build\Debug\editor-cli.exe demo .\build\demo.nle
.\build\Debug\editor-cli.exe inspect .\build\demo.nle
.\build\Debug\editor-cli.exe new .\build\example.nle "My project"
.\build\Debug\editor-cli.exe add-sequence .\build\example.nle Main
~~~

On Linux and macOS the executable is ./build/editor-cli.
The demo creates video/audio tracks and a logical asset, inserts, moves, trims,
splits and deletes a clip, exercises undo/redo, saves, reloads, and checks equality.
It leaves two editable video clips and an empty audio track. It does not read demo.mov.
**demo and session-demo replace their output files; new rejects existing files.**
The session-demo previews seven commands, commits them as one edit, undoes/redoes the entire batch, and verifies save/load. Revisions and attribution persist. Undo/redo is session-local; separate CLI invocations start new sessions.

## Editing contract

- IDs are strong C++ types, stable across moves, saves, and reloads.
- Rational time uses nonnegative reduced integer fractions, never floating seconds.
- Clips play at speed 1. Source and timeline ranges are half-open.
- Tracks reject overlaps. Gaps and adjacent clips are allowed.
- Moves may change track/sequence. Trims explicitly set position and source range.
- Splits retain the left ID and create a right ID. Delete does not ripple.
- Invalid edits leave state and history unchanged. New edits discard redo.
- Transactions preview privately; failed or stale batches publish nothing.
- Track deletion/reordering and original/proxy relinking are undoable.
- Editor entry points are synchronized; external edits should supply expected revision.
- Snapshots are independent values; callers cannot mutate a live editor through them.

See [architecture](docs/architecture.md), [native format](docs/native-format.md),
[editing sessions](docs/editing-sessions.md), [MCP boundary](docs/mcp-boundary.md), [roadmap](docs/roadmap.md), and
[decisions](docs/adr/README.md).

## Benchmarks

~~~powershell
cmake -S . -B build -DNLE_BUILD_BENCHMARKS=ON
cmake --build build --config Release
.\build\Release\nle-benchmark.exe
~~~

See [performance and validation](docs/performance.md) for measured budgets and results.

## Current limits

History defaults to 100 entries/64 MiB of accounted payload, not an RSS ceiling.
Transactions allow 1,024 staged commands; attribution caps at 10,000 operations.
Oversized edits and full audit logs reject changes explicitly. Native files remain
limited to 16 MiB/100,000 nested records; version 1 loads migrate to version 2 on save.
Open transactions and undo history are not persisted.

There is no independent-writer coordination, retry idempotency, authenticated attribution,
audit compaction, power-loss durability or crash recovery. Use one authoritative Editor
per project. Exact time arithmetic retains milestone 1's conservative overflow limits;
signed offsets, snapping, speed changes, linked A/V edits and transitions remain deferred.

Original code is [MIT licensed](LICENSE). No third-party runtime library is introduced.
Future dependency choices must document distribution and licensing requirements.