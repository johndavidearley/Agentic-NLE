# Milestone 7 validation

Local implementation verified on Windows x64, 2026-09-16, with MSVC 19.44, Qt 6.10.3,
clang-format 19.1.5 and the existing FFmpeg/ffprobe 9.0.1 fixtures. Remote platform validation
is pending; this record does not infer other platform results from Windows.

## Added checks

The default C++ document-files suite checks exclusive writer ownership, read-only access,
new-file conflicts, external changes, Unicode paths, two-slot recovery, truncated-newest
fallback, untitled checkpoints, explicit recovery/discard, foreign baseline rejection and
Save As identity retention. Windows replacement-lock tests cover both save and checkpoint
write failure. Unwritable recovery destinations are exercised with a directory occupying a
slot. Cleanup failure after a successful save remains a warning. Tests do not fill a real disk
or simulate power loss.

The Qt recovery suite exercises actual desktop document methods and edit controls: dirty
checkpoints leave the saved file unchanged; a new window restores an older valid checkpoint;
recovered work has no invented undo stack; Save As preserves the original file and identity;
untitled drafts recover; explicit discard opens the saved version. No media files are needed
for this suite, including its offline-timeline case.

The real MCP stdio suite additionally checks a competing CLI write, forced process termination
after two completed checkpoints, fallback after truncation, an explicit recovery startup
choice, preserved dirty status with fresh history, successful save/cleanup, and a checkpoint
failure reported independently from a successful and retryable edit.

## Platform investigation

The latest remote run observed when this milestone began was
[Milestone 5 run 34883812269](https://github.com/johndavidearley/Agentic-NLE/actions/runs/34883812269).
Core builds, sanitizers and media integration passed there across the configured platforms;
desktop passed on Windows and Intel macOS. Linux desktop failed for missing Qt ICU 73;
Apple Silicon desktop timed out in the longer playback observation.

Milestone 6 already added installation of Qt's ICU archive to the Linux desktop job. Milestone
7 separates the long-playback preparation wait from the measured playback interval and adds
status, position and observation-count diagnostics on failure. The original requirement to
reach eleven seconds of playback within sixteen seconds and existing frame/skew/drift checks
are retained. This removes preparation time from the measurement; it is not yet evidence that
the Apple Silicon failure is fixed. Remote execution on the milestone revision is pending.

The first Milestone 7 run,
[35100708760](https://github.com/johndavidearley/Agentic-NLE/actions/runs/35100708760), tested
`60a2c63b86026a9e73193f1a7b31f99905bda61b`: 27/28 jobs passed, including Windows desktop,
both macOS desktop architectures, every core/media job and all eight MCP jobs. Linux passed
its document and Qt recovery tests but failed the three playback tests. Inspecting the pinned
Linux FFmpeg plugin with `ldd` showed direct Qt Quick/QML dependencies omitted by the reduced
SDK installation. The follow-up installs the matching `qtdeclarative` archive and checks
plugin dependencies before tests. The full follow-up run remains pending.

## Local results

| Check | Result | Elapsed |
| --- | --- | ---: |
| Complete Windows Debug suite | 38/38 passed | 66.66 s |
| Complete Windows Release suite | 38/38 passed | 50.30 s |
| Independent default Release build, Qt/MCP disabled | 28/28 passed | 2.28 s |
| Independent Qt-free MCP Release build | 31/31 passed | 6.43 s |
| Final MCP Debug checks after recovery-status refinements | 3/3 passed | 5.95 s |
| Final MCP Release checks after recovery-status refinements | 3/3 passed | 3.14 s |

All builds use warnings as errors. clang-format 19.1.5 format-check passed. The last MCP-only
refinements clear a checkpoint warning after a successful explicit save and avoid reporting
a new checkpoint for an unchanged undo/redo request. Permission and no-op cases are exercised
through the actual server process. No core/desktop behavior changed after the complete suites.

The deployed Release desktop passed both the recovery and media/editing smoke tests with
QT_PLUGIN_PATH cleared. Its screenshot was inspected: Save As and Recover draft fit the
project toolbar and the preview/timeline/inspector remain visible. Deployment still omits
translation catalogs and no installer was produced. Native version 4 and existing editing
invariants remain intact. These local checks include the official MCP SDK 2.2.0 round trip.

Remote core/media/desktop/MCP runs must be recorded before Milestone 7 is marked complete.
