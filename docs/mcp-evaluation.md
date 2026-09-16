# Milestone 6 MCP evaluation

Executed 2026-09-14 on Windows x64 with MSVC 19.44, C++20, nlohmann/json 3.12.0,
Python 3.11.15 and the official MCP Python SDK 2.2.0. The server is a native C++ process;
Python is used only by tests. [ADR 0012](adr/0012-local-mcp-adapter.md) records dependencies,
protocol scope and the local trust boundary.

## Verification

| Configuration | Result | Elapsed |
| --- | --- | ---: |
| Complete Debug suite, including desktop/media/MCP | 36/36 passed | 63.78 s |
| Complete Release suite, including desktop/media/MCP | 36/36 passed | 46.53 s |
| Final MCP Debug checks after save-budget refinement | 3/3 passed | 4.50 s |
| Final MCP Release checks after save-budget refinement | 3/3 passed | 2.96 s |
| Independent default Release build, MCP and Qt disabled | 27/27 passed | 2.47 s |
| Independent MCP Release build, Qt and media integration disabled | 30/30 passed | 5.36 s |

Warnings are treated as errors. clang-format 19.1.5 format-check passed. The final refinement
reserves enough retry-ledger capacity for one save attempt after normal admission fills;
its focused tests cover both entry-count and byte-budget exhaustion. No core or desktop
implementation changed in this milestone. Desktop/media regression runs use the existing
Qt 6.10.3, Qt FFmpeg 7.1.3 and external FFmpeg/ffprobe 9.0.1 setup.

The C++ session test compares a complete agent batch against direct core commands, including
snapshot contents, exact IDs/time, grouped history and actor/operation metadata. It covers
failed partial proposals, wrong-kind/unknown aliases, stale and competing proposals, undo/
redo, denied permissions, actor spoofing, foreign project/session IDs, bounded audit pages,
expiry, proposal disposal, and request-key reuse. Retrying a commit after undo/redo returns
its original result without repeating the edit. Admitted failures are also replayed.

C++ protocol checks cover duplicate JSON members, nesting limits and invalid message shapes.
The real stdio process test exercises lifecycle, nine-tool discovery, malformed input and
recovery, message-size limits, Unicode project paths, explicit save,
rejected path arguments, two cooperative writers, external file changes, process closure
without save, lock release and opening a fresh session. Symlink replacement is checked when
the host permits creating symlinks; this does not establish protection against filesystem
races by noncooperating writers.

## Official client interoperability

The official MCP SDK's high-level Client automatically negotiates this server's
2025-11-25 profile. Each successful run discovers all nine tools, validates their JSON
schemas, validates tool inputs and structured outputs, and checks text/structured result
agreement. The workflow makes 24 tool calls and applies a ten-command batch covering all
nine supported command types. It discards a preview, commits another as one operation,
retries the commit, undoes/redoes it, saves/retries, and checks persisted agent attribution
and native CLI reload. Frame duration and source-in retain the exact 1001/30000 fraction.

| Final main-build SDK run | Observed workflow time |
| --- | ---: |
| Debug | 389.04 ms |
| Release | 165.35 ms |

These are individual local executions, including process/client setup; they are not latency
percentiles or performance guarantees. [Raw Debug results](mcp-evaluation-Debug.json) and
[raw Release results](mcp-evaluation-Release.json) preserve the observations. This is protocol
interoperability evidence, not a test of every MCP application's configuration or UI.

## Reproduce

~~~sh
python -m venv build/mcp-env
# Activate that environment using the platform's normal activation command.
python -m pip install mcp==2.2.0
cmake -S . -B build -DNLE_BUILD_MCP=ON -DNLE_MCP_SDK_INTEGRATION=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
~~~

Set Python3_EXECUTABLE and NLE_MCP_SDK_PYTHON to the environment's Python executable when
CMake would otherwise find a different installation. The optional SDK test checks version
2.2.0 at configure time. Without NLE_MCP_SDK_INTEGRATION, the adapter still has the C++
session test and standard-library Python stdio test. BUILD_TESTING=OFF needs no Python.
For offline builds, set FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON to verified 3.12.0 sources.
See [agent setup and workflow](agent-editing.md) for permissions and client arguments.

## Platform evidence and limits

The new MCP CI matrix configures Debug and Release on Windows, Linux and macOS Apple
Silicon/Intel without Qt. Those remote runs are pending; only Windows was executed locally.

Inspection of the prior [Milestone 5 CI run](https://github.com/johndavidearley/Agentic-NLE/actions/runs/34883812269)
found a missing Qt ICU 73 runtime on Linux and a timed-out long-playback observation on
macOS Apple Silicon. This milestone adds Qt's ICU archive to Linux desktop setup. That
setup change awaits CI verification. The existing macOS playback failure remains unresolved;
its check is retained unchanged and is separate from the new headless MCP adapter.

The launcher fixes one project path, actor and independent edit/save permissions. Media
must already be registered. Save compares existing bytes and uses native atomic replacement,
with a cooperative sidecar lock for saving MCP processes. A noncooperating desktop can still
race that comparison: use one writer per project. No import/relink, arbitrary paths, remote
transport, live desktop integration, asynchronous jobs or media execution are exposed.

Sessions last one hour; proposals last two minutes. Save before session expiry. A finite
retry ledger retains results for that process only and reserves one final save attempt after
the normal budget fills. Expiry, process failure or EOF does not save unsaved edits, and
there is no durable replay journal. Application message/project/history budgets do not
constitute a whole-process memory or operating-system sandbox. Native version 4 and the
Qt-free command core remain unchanged.

## Subsequent platform validation

Milestone 7 subsequently passed the complete platform matrix on 2026-09-16 at `b2978a0`.
See [the recovery/platform validation record](recovery-evaluation.md) for the exact run,
follow-up fixes and results. The measurements above remain the original milestone record.
