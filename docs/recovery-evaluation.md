# Milestone 7 validation

Local implementation verified on Windows x64, 2026-09-16, with MSVC 19.44, Qt 6.10.3,
clang-format 19.1.5 and the existing FFmpeg/ffprobe 9.0.1 fixtures. Milestone 7 is complete:
all 28 remote CI jobs passed on `b2978a01ce34bd03a9d6c36a190f1e7fd6f82877`, covering
Windows, Linux and macOS Apple Silicon/Intel. The completion commit changes documentation
only; the tested implementation and workflow revision is identified explicitly here.

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
are retained. This removes preparation time from the measurement. Both macOS architectures
passed the first Milestone 7 run recorded below.

The first Milestone 7 run,
[35100708760](https://github.com/johndavidearley/Agentic-NLE/actions/runs/35100708760), tested
`60a2c63b86026a9e73193f1a7b31f99905bda61b`: 27/28 jobs passed, including Windows desktop,
both macOS desktop architectures, every core/media job and all eight MCP jobs. Linux passed
its document and Qt recovery tests but failed the three playback tests. Inspecting the pinned
Linux FFmpeg plugin with `ldd` showed direct Qt Quick/QML dependencies omitted by the reduced
SDK installation. The follow-up installs the matching `qtdeclarative` archive and checks
plugin dependencies before tests. Linux then passed all 35 desktop-job tests in 27.95 seconds
on `b2978a01ce34bd03a9d6c36a190f1e7fd6f82877` in
[run 35107972182](https://github.com/johndavidearley/Agentic-NLE/actions/runs/35107972182).

The combined revision also includes the independently merged PR #3 (`e304c7f`): shared
20-second worker/transport readiness deadlines and preview-image suppression under socket
backpressure. The local Release desktop checks were repeated after integrating that change;
all five tests, including the media fixture, passed in 43.06 seconds. Format-check passed again.
The full follow-up platform run passed all 28 jobs.

## Local results

| Check | Result | Elapsed |
| --- | --- | ---: |
| Complete Windows Debug suite | 38/38 passed | 66.66 s |
| Complete Windows Release suite | 38/38 passed | 50.30 s |
| Independent default Release build, Qt/MCP disabled | 28/28 passed | 2.28 s |
| Independent Qt-free MCP Release build | 31/31 passed | 6.43 s |
| Final MCP Debug checks after recovery-status refinements | 3/3 passed | 5.95 s |
| Final MCP Release checks after recovery-status refinements | 3/3 passed | 3.14 s |
| Release desktop checks after integrating PR #3 | 5/5 passed | 43.06 s |

All builds use warnings as errors. clang-format 19.1.5 format-check passed. The last MCP-only
refinements clear a checkpoint warning after a successful explicit save and avoid reporting
a new checkpoint for an unchanged undo/redo request. Permission and no-op cases are exercised
through the actual server process. The later PR #3 desktop changes were checked separately
as recorded above; core and recovery behavior were unchanged.

The deployed Release desktop passed both the recovery and media/editing smoke tests with
QT_PLUGIN_PATH cleared. Its screenshot was inspected: Save As and Recover draft fit the
project toolbar and the preview/timeline/inspector remain visible. Deployment still omits
translation catalogs and no installer was produced. Native version 4 and existing editing
invariants remain intact. These local checks include the official MCP SDK 2.2.0 round trip.

## Remote platform results

[Run 35107972182](https://github.com/johndavidearley/Agentic-NLE/actions/runs/35107972182)
completed successfully on 2026-09-16 at `b2978a01ce34bd03a9d6c36a190f1e7fd6f82877`.
[Saved CI evidence](recovery-ci-results.json) preserves each job's name/result/link and the
four desktop precision reports independently of this conversation and expiring CI artifacts.

| Job group | Coverage | Result |
| --- | --- | --- |
| Core | Windows, Linux GCC/Clang, macOS Apple Silicon/Intel; Debug and Release | 10/10 jobs passed |
| Media integration | Windows, Linux, both macOS architectures | 4/4 jobs passed |
| Desktop | Windows, Linux, both macOS architectures; Release | 4/4 jobs passed; 35 tests each |
| Headless MCP and official SDK | Windows, Linux, both macOS architectures; Debug and Release | 8/8 jobs passed |
| Sanitizers | Linux Clang | Passed |
| Formatting | clang-format 19 | Passed |

The Windows desktop job also passed its deployed-runtime smoke test. Linux and both macOS
CI screenshots were inspected: preview, timeline, inspector and recovery controls are visible.

Precision checks retained their original bounds. The long A/V fixture starts video at zero
and audio at 500,000 microseconds, and reaches eleven seconds within the measured sixteen-second
playback interval. Recorded decoder-delivery measurements are:

| Platform | Maximum selected-frame PTS error (us) | Maximum A/V delivery skew (us) | Mean offset drift (us) |
| --- | ---: | ---: | ---: |
| Windows x64 | 1 | 7,152 | 356.70 |
| Linux x64 | 1 | 9,152 | 45.32 |
| macOS Apple Silicon | 1 | 19,324 | 519.67 |
| macOS Intel | 1 | 11,132 | 729.41 |

These observations measure decoder output with audible playback disabled, not physical
speaker/display synchronization. Packaging, multi-track playback and power-loss durability
remain outside Milestone 7.
