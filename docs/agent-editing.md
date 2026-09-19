# Agent editing through MCP

Milestone 6 adds a local MCP server for inspecting and editing one native project. Agent
changes use the same commands, validation and undo engine as desktop edits. Save the result,
close the agent session, and reopen the project in the desktop to continue editing it.
Media must already be registered through the desktop or CLI.

## Build and connect

~~~powershell
cmake -S . -B build -DNLE_BUILD_MCP=ON
cmake --build build --config Release
ctest --test-dir build -C Release -R "^mcp-" --output-on-failure
~~~

The server requires C++20 and nlohmann/json 3.12.0. CMake uses an installed matching package
or downloads the pinned, hash-verified archive when MCP is enabled. Default builds remain
unchanged. An offline build can set FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON to an extracted
3.12.0 source directory. Python 3 is needed for tests; BUILD_TESTING=OFF builds the server
without Python. Qt, FFmpeg and a Python MCP package are not server runtime dependencies.

Select an existing project. For a new one, create it and import media first, for example:

~~~powershell
.\build\Release\editor-cli.exe new .\build\agent-project.nle "Agent project"
.\build\Release\editor-cli.exe import .\build\agent-project.nle "D:\media\take.mp4" "D:\tools\ffprobe.exe"
~~~

Configure a client supporting local stdio MCP and protocol 2025-11-25 with this executable
and argument list. Adapt the following common JSON layout to the client's configuration:

~~~json
{
  "mcpServers": {
    "agentic-nle": {
      "command": "D:/Agentic NLE/build/Release/editor-mcp.exe",
      "args": [
        "--project", "D:/Agentic NLE/build/agent-project.nle",
        "--actor", "agent:editor",
        "--allow-edit",
        "--allow-save"
      ]
    }
  }
}
~~~

Linux/macOS use the built editor-mcp executable with the same arguments. The MCP client
launches the process and communicates over its input/output pipes. No server port is opened.
A console started manually waits for protocol messages; it is not an interactive prompt.

Permissions come from the launcher, not from the agent's tool arguments:

| Launch configuration | Allowed behavior |
| --- | --- |
| No permission flags | Inspect and prepare/discard detached proposals |
| --allow-edit | Also commit, undo and redo in memory |
| --allow-save | Also write the one configured project file |

The actor ID is fixed for the process and all committed operations have Agent attribution.
A local client's process launch and operating-system account are the trust boundary; neither
clientInfo nor a tool argument can claim another actor or grant permissions. Use one writer
per project. Close the desktop document before an agent edits its saved file, or use a copy.

## Editing workflow

1. Call project_get for the project ID, session ID, revision, permissions and history status.
2. Call project_snapshot with project_id to inspect media IDs, sequences, tracks and clips.
3. Send edit_preview with commands, a descriptive label, the current expected_revision,
   session_id, project_id and a fresh request_key. The returned snapshot is provisional;
   the live project, undo history and saved file are unchanged.
4. Inspect the proposal. Call edit_commit with its proposal_id and a new request_key, or
   edit_rollback to discard it. A successful commit is one undoable edit, regardless of the
   number of commands. A changed base revision requires a new preview.
5. Use history_undo/history_redo as needed. Call project_save explicitly to retain the work.
   Closing stdin ends the session and discards unsaved changes and session undo history.

For example, the commands field can build a sequence using aliases for newly allocated IDs:

~~~json
[
  {"op":"create_sequence","name":"Main","as":"main","frame_duration":{"value":"1001","rate":"30000"}},
  {"op":"create_track","sequence_id":"$main","kind":"video","name":"V1","as":"video"},
  {"op":"insert_clip","track_id":"$video","media_id":"4","position":{"value":"0","rate":"1"},"source_in":{"value":"0","rate":"1"},"duration":{"value":"2","rate":"1"},"as":"opening"}
]
~~~

Replace media_id with one from the snapshot. IDs/revisions are unsigned decimal strings;
object kind comes from the field name and each request is scoped to project_id. Rational
seconds use value/rate string pairs, never approximate floating-point seconds. Aliases are
local to the batch; $main cannot refer to a different proposal. Preview IDs are provisional
until commit. Unknown fields, commands, aliases, kinds and invalid ranges reject the batch.

## Available tools

| Tool | Purpose |
| --- | --- |
| project_get | Identity, session, permissions, revision, dirty flag, history and remaining lifetime |
| project_snapshot | Detached media and timeline data; no media file access |
| project_changes | Up to 100 audit records after since_revision; continue at next_revision |
| edit_preview | Prepare 1-128 timeline commands and inspect their provisional result |
| edit_commit | Publish a proposal as one validated undoable edit |
| edit_rollback | Discard a proposal |
| history_undo / history_redo | Apply retained history with fixed agent attribution |
| project_save | Save to the configured file if it has not changed externally |

Supported commands: create_sequence, create_track, insert_clip, move_clip, trim_clip,
split_clip, delete_clip, delete_track and reorder_track. Tool discovery provides complete
input schemas and annotations. Both text and structuredContent return the same result;
errors set isError and include a stable error code and a short message.

## Retries, expiry and file protection

Every stateful request, including preview/rollback/save, needs a request_key. Retry an uncertain
request with exactly the same arguments and key; the JSON-RPC message ID may change. The server
returns the recorded result even if the project revision has advanced. Reusing the key with
different arguments returns idempotency_conflict. Admitted tool failures are recorded too: use a
new key after correcting arguments. Invalid context and admission-limit failures create no
record. Never infer that an old cached preview is still active.

A session lasts one hour. Proposals expire after two minutes and at most four can be active;
expiry is checked at tool entry. EOF frees all proposals. The retry ledger allows 256 normal
recorded requests and reserves one final save-only entry. Previews and recorded failures count.
It also has a 32 MiB serialized-payload budget; entries are never silently evicted. On
session_limit, save the current project using the reserved request and restart. Save before
session_expired: expiry prevents further tool operations. Retry guarantees end with the process;
there is no crash-safe replay journal. Optional --recovery checkpoints can retain unsaved
state; see [project recovery](project-recovery.md) for explicit startup choices and limits.

The configured native file is the only writable project path; no path, Save As, import,
relink, shell or arbitrary-file tools exist. Saving sessions acquire an adjacent .mcp-lock
file lock, which now coordinates desktop, CLI and saving/recovery-enabled MCP sessions and releases on exit/crash.
The empty sidecar remains for safe reuse. Existing project bytes are compared before saving;
a missing/replaced/changed file returns save_conflict without overwriting it. Undo/redo and
unsaved in-memory state are still available after a failed save. This check is not atomic
coordination with a noncooperating writer between comparison and replacement. Existing recovery
files require an explicit recover/discard choice before saving over them.

Requests are limited to 1 MiB and 64 JSON nesting levels; duplicate object members reject.
An oversized input line closes the connection. Results are capped at 2 MiB before MCP's text/
structured duplication. The project profile is 8 MiB of accounted snapshot data; proposals
are capped at 32 MiB in total. Core history retains its existing limits. These are application
budgets, not a whole-process memory/security sandbox. Requests execute serially; cancellation
cannot interrupt an atomic core edit. No asynchronous jobs or background media access occur.

See [MCP boundary](mcp-boundary.md), [ADR 0012](adr/0012-local-mcp-adapter.md), and
[MCP validation](mcp-evaluation.md) for the implementation contract and executed checks.

## Milestone 8 settings

`set_sequence_output` accepts sequence_id, exact frame_duration, and output with integer
width, height, sample_rate=48000 and channels=2. `set_track_playback` accepts track_id,
boolean enabled/muted and integer gain_milli (0..4000; 1000 is unity).
`set_clip_routing` accepts clip_id and routing containing video/audio selections. Each
selection is {"mode":"auto"}, {"mode":"disabled"}, or {"mode":"stream","index":N}.
`insert_clip` also accepts optional routing. Explicit indices require existing probed metadata.
These are commands within the existing preview/commit protocol, with the same revision,
permission, transaction and undo rules. The tool catalog supplies their strict JSON schemas.
