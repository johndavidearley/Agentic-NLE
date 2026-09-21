# Milestones 8 and 9 platform validation

All 28 jobs in [run 35595674717](https://github.com/johndavidearley/Agentic-NLE/actions/runs/35595674717)
passed on 2026-09-21 at commit 3323cea28878f2d6c4561244ce51e686b7c5bd89.
[Saved results](desktop-ci-results.json) preserve job links, playback reports and timeline
measurements independently of this conversation and expiring CI artifacts.

Together with the existing [ten-minute playback acceptance](multitrack-evaluation.md)
and [timeline acceptance](timeline-evaluation.md), this completes Milestones 8 and 9.

## Corrections verified

- MSVC suppresses warnings only inside the external FFmpeg header wrapper; project
  warnings remain errors, and matching prepared SDK headers keep priority.
- Cache workers keep structured stdout separate from Qt stderr diagnostics. Regression
  tests force plugin logging and cover merged/default output, discarded diagnostic floods,
  nonzero exits and timeouts.
- Playback workers flush local-socket writes promptly. Worker and receiving-process
  lateness diagnostics remain visible; the original 20 ms drift limits are unchanged.
- Include ordering matches CI clang-format 19.1.1. CI prints the formatter version, and
  the README records the known difference from Visual Studio's bundled 19.1.5.

The complete local Windows Release suite passed 50/50 with warnings as errors, including
the new process/cache regressions. The matching format check also passed.

## Platform results

| Job group | Coverage | Result |
| --- | --- | --- |
| Core | Windows, Linux GCC/Clang, macOS Apple Silicon/Intel; Debug and Release | 10/10 jobs passed |
| Media integration | All four platform targets | 4/4 jobs passed |
| Desktop | All four platform targets; Release | 4/4 jobs passed; 47 tests each |
| Headless MCP and official SDK | All four platform targets; Debug and Release | 8/8 jobs passed |
| Sanitizers | Linux Clang | Passed |
| Formatting | clang-format 19.1.1 | Passed |

Windows also passed deployed-runtime verification. All desktop jobs passed the cache
regression, editing/workflow checks at 100%, 150% and 200% scaling, precision seeking,
worker supervision and multi-track playback.

| Platform | Seek p95 ms | Cancel ms | Presentation drift ms | A/V delivery drift ms |
| --- | ---: | ---: | ---: | ---: |
| Windows x64 | 529 | 3 | 1.582 | 0.532 |
| Linux x64 | 445 | 3 | 0.072 | 0.253 |
| macOS Apple Silicon | 335 | 5 | 6.299 | 1.924 |
| macOS Intel | 634 | 8 | 1.099 | 1.012 |

Each platform's twelve-second playback check had zero audio sample, frame-selection and
black-gap errors, zero dropped frames and zero underruns. All original timing and queue
limits passed. Timeline responsiveness values are retained as CI diagnostics; the fixed-host
acceptance target remains the previously measured Windows result.

These tests use generated media and inaudible playback. They measure worker/decoder delivery,
not physical speaker/display synchronization. The ten-minute run remains a Windows local
measurement; installers and export remain future milestones.
