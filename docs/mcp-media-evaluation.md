# Milestone 11 media preparation evaluation

Recorded 2026-09-30 on Windows Release with warnings as errors. This is local development-machine
evidence, not yet a cross-platform support claim. The media-only MCP CI matrix is configured for
Linux, Windows, macOS Apple Silicon and macOS Intel; its results have not been recorded here.

The launcher grants media access separately from editing and saving. It accepts one to eight
canonical, non-filesystem-root directories and one fixed absolute probe executable. Component-wise
canonical path checks reject sibling directories, traversal and symlink escapes. An ordinary
nine-tool inspection/editing session does not expose media operations.

The real stdio server and generated media corpus passed bounded asynchronous probe, ready-status
inspection, approved Unicode import, verified moved-source relink, cancellation, corrupt input,
missing and out-of-root paths, changed-file refusal, stale-revision refusal, undo/redo, explicit
save and idempotent start/commit retries. A media-only grant could probe but not commit an edit.
The official MCP SDK 2.2.0 discovered and validated all 14 granted tool schemas, imported into a
launcher-created empty project, built a rough cut, relinked a moved source, saved, then reopened
the native project through a new inspection-only session. `mcp-media`, `mcp-media-sdk` and their
generated corpus fixture passed locally (3/3). The existing default nine-tool SDK test remains
separate. The complete Windows Release CTest suite passed 52/52 with two parallel workers;
clang-format 19.1.1 passed for the changed C++ files, and `git diff --check` passed.

The optional `editor-mcp-export` launcher exposes 17 tools with media and output grants, while
`editor-mcp` remains independent of linked FFmpeg/Qt. Export captures a detached revision and
reports uncached progress; one worker can run at once and sixteen results are retained per session.
Output grants reserve the native project/sidecars and reject escaping parents, symlinks and media
replacement. Grants are rechecked immediately before staged output installation. Cancellation
requests do not claim cancellation when installation already succeeded.

The official-client export workflow imports approved Unicode media, builds a two-second rough
cut, relinks, saves and exports a Unicode destination as FFV1/float PCM Matroska. An independent
ffprobe checks codecs and duration; the result reports 96,000 samples and the captured revision
despite a concurrent in-memory undo. Tests cover project/outside-root denial, idempotent start,
late cancellation, existing-output preservation without overwrite, and cancellation through a
read-only export grant. Unit coverage includes deterministic cancellation, stale starts, the
one-worker limit, hard-link/project protection and sidecar/path boundaries. This workflow found
and fixed Windows export staging paths passed to FFmpeg in a narrow encoding rather than UTF-8.
After this export slice, the complete Windows Release CTest suite passed 53/53 with two workers.
The warnings-as-errors build, clang-format 19.1.1 checks, CI YAML parse and `git diff --check`
also passed. No cross-platform run is inferred from these local results.

Still open: cross-platform CI results and the full import-to-export desktop comparison.
The desktop CI matrix now includes the official-client export check; it has not run here.
Canonical path checks constrain cooperative use of
the local service; they are not an operating-system sandbox against filesystem races or hostile
processes running under the same account.
