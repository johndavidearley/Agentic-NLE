# Agentic NLE

A professional open-source video editor designed for human editors and software agents.
This repository implements **Milestone 1 — Headless Timeline Core**.
There is no graphical editor, decoder, playback engine, or MCP server yet.

The C++20 core provides typed persistent identities, exact rational time, detached
inspection snapshots, validated commands, undo/redo, and versioned native persistence.
Human UI, CLI, and future MCP adapters share the same command boundary.

## Build and test

Requires CMake 3.24+ and a C++20 compiler. No third-party libraries or downloads
are required. Locally verified with MSVC 19.44 on Windows x64; CI also targets GCC.

PowerShell with Visual Studio 2022 C++ Build Tools installed:

~~~powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
~~~

Linux:

~~~sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
~~~

Warnings are errors. Formatting uses clang-format 19. Set
-DCLANG_FORMAT=/path/to/clang-format when configuring if it is not on PATH,
then build targets **format** or **format-check**.

## Run the vertical slice

~~~powershell
.\build\Debug\editor-cli.exe demo .\build\demo.nle
.\build\Debug\editor-cli.exe inspect .\build\demo.nle
.\build\Debug\editor-cli.exe new .\build\example.nle "My project"
.\build\Debug\editor-cli.exe add-sequence .\build\example.nle Main
~~~

On Linux the executable is ./build/editor-cli.
The demo creates video/audio tracks and a logical asset, inserts, moves, trims,
splits and deletes a clip, exercises undo/redo, saves, reloads, and checks equality.
It leaves two editable video clips and an empty audio track. It does not read demo.mov.
**demo replaces its output file; new rejects existing files.**
Undo/redo is session-local; separate CLI invocations start new sessions.

## Editing contract

- IDs are strong C++ types, stable across moves, saves, and reloads.
- Rational time uses nonnegative reduced integer fractions, never floating seconds.
- Clips play at speed 1. Source and timeline ranges are half-open.
- Tracks reject overlaps. Gaps and adjacent clips are allowed.
- Moves may change track/sequence. Trims explicitly set position and source range.
- Splits retain the left ID and create a right ID. Delete does not ripple.
- Invalid edits leave state and history unchanged. New edits discard redo.
- Snapshots are independent values; callers cannot mutate a live editor through them.

See [architecture](docs/architecture.md), [native format](docs/native-format.md),
[MCP boundary](docs/mcp-boundary.md), [roadmap](docs/roadmap.md), and
[decisions](docs/adr/README.md).

## Current limits

Whole-project snapshots make history costly for large projects. Time arithmetic rejects
overflow, including some representable results with intermediates exceeding 64 bits.
There is no signed offset, frame snapping, speed change, linked A/V editing, transition,
track deletion/reordering, concurrent writer, or transaction API.
Native files are capped at 16 MiB and 100,000 nested records. No historical schema
migration or crash recovery is promised. Saves replace through an adjacent temporary
file; power-loss durability is not guaranteed.

Original code is [MIT licensed](LICENSE). Future dependency choices must document their
own distribution and licensing requirements.
