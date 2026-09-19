# Milestone 8 playback prototype

Status: gate passed on the named Windows host, 2026-09-16. The integrated worker's
independent [Windows production acceptance](multitrack-evaluation.md) passed on 2026-09-18.

## Fixed targets

Named local host: Typhoon, Windows x64, AMD Ryzen 5 2600 (6 cores / 12 logical processors),
17,101,860,864 bytes installed RAM. MSVC 19.44, Qt 6.10.3, Qt FFmpeg 7.1.3; fixture tools
FFmpeg 9.0.1. Compare the existing supervised Qt players with direct decoded video/PCM.

Profile: two 1920x1080 SDR video tracks at 30 or 30000/1001 fps (including VFR), four stereo
48 kHz audio tracks, speed 1, hard cuts, gaps and source offsets. Preview may be 960x540.
The prototype gate is a 60-second run; final acceptance requires the full ten-minute sequence.

Targets are set before measuring:

- Initial readiness <= 3 seconds; p95 seek readiness <= 1 second; cancellation <= 250 ms.
- One output sample clock; zero missing/misaligned reference audio samples or silent cut gaps.
- Correct held video frame at every sampled timestamp; numerical timestamp error <= 2 us.
- Steady-state underruns: zero after initial prebuffer; dropped presentation frames <= 1%.
- Decoder-delivery A/V drift <= 20 ms over the reference run; no physical device timing claim.
- Application-owned decoded queues <= 64 MiB; peak process-tree working set <= 1 GiB.
- Every old generation is discarded after seek/stop; failure must not change the project.

Audio is stereo float at 48 kHz, summed in track order with explicit linear gain and hard
clamping to [-1, 1]. No automatic normalization. Top enabled video wins, preserving each
clip's own embedded-audio routing independently of visual coverage. Black fills gaps;
aspect-preserving letterboxing uses square-pixel output. These semantics must be represented
in the eventual plain C++ plan, native migration and undoable/MCP commands.

## Dependencies under evaluation

Direct FFmpeg uses the already-installed Qt 7.1.3 DLLs and upstream 7.1.3 public headers.
Source archive SHA-256: f0bf043299db9e3caacb435a712fc541fbb07df613c4b893e8b77e67baf3adbe.
The prototype imports those DLLs without using Qt private APIs. Headers/import libraries live
only in ignored build output. A final ADR must record the selected backend and distribution
consequences after the measured comparison. No runtime download is introduced into CMake.

[FFmpeg APIs](https://ffmpeg.org/doxygen/7.1/),
[Qt audio buffer API](https://doc.qt.io/qt-6.10/qaudiobufferoutput.html),
[FFmpeg licensing](https://ffmpeg.org/legal.html).

## Measured comparison

Raw numerical results are retained in [the result record](multitrack-prototype-results.json).

| Measurement | Existing Qt workers, 60 s | Direct FFmpeg, 60 s | Direct FFmpeg, 600 s |
| --- | ---: | ---: | ---: |
| Initial readiness | 2,259 ms | 293 ms | 222 ms |
| Seek p95 | 346 ms | 106 ms | 82 ms |
| Cancellation | 30 ms | 5 ms | 5 ms |
| Peak process-tree RSS | 426 MiB | 82 MiB | 81 MiB |
| Audio sample errors / underruns | No shared PCM mixer | 0 / 0 | 0 / 0 |
| Late video frames | Not comparable | 0 | 0 |
| Maximum frame PTS error | Not compared | 1 us | 1 us |
| Application decoded queue | Not measured | 15 MiB | 15 MiB |

The six independent Qt clocks diverged by up to 880 ms. Its protocol did not expose PCM
for shared mixing, so that baseline cannot satisfy the multi-track audio contract unchanged.
The direct candidate passes the fixed prototype gate and is selected in ADR 0014.

These prototype results are not production acceptance. Readiness excludes independent
ffprobe oracle preparation. The prototype schedules video at the first 10 ms block crossing
each output frame time; production must use the exact output frame grid. Its reported clock
drift (7.28 ms over 600 s) is change in delivery lateness against a monotonic deadline, not
measured physical A/V drift. RSS includes the harness and children sampled every 25 ms.
The Qt loading-observation count is a sampling count, not a duration of cut gaps.

VFR, B-frame, positive-offset and negative-origin precision fixtures passed after correcting
negative-prefix seeking. The final production worker must repeat these checks, verify exact
fractional boundaries, discarded generations, output PCM, cuts, gaps and the same timing limits.
