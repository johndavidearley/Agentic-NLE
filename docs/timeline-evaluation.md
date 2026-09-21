# Milestone 9 timeline evaluation

Status: Windows local acceptance passed on 2026-09-20. Platform CI remains pending.
The preceding Milestone 8 baseline is commit 17a4a61, with its prototype and ten-minute
production acceptance recorded in [multitrack validation](multitrack-evaluation.md).
The recorded source hashes identify the locally tested Milestone 9 implementation.

Before measurement, the Release responsiveness target on Typhoon (Ryzen 5 2600,
six cores/twelve threads, 16 GiB RAM, Windows x64) is p95 <= 16 ms for a provisional
drag update plus synchronous visible viewport paint on a 1,000-clip project.
Gesture initiation target: <= 50 ms. Run 120 updates after first paint at a
1360 x 540 logical-pixel viewport, with snapping enabled, including validation.
Report maximum and median as diagnostics. Cache work runs separately and never
decodes on the UI thread. CI records measurements without a shared-runner timing
assertion; local completion requires the published target to pass.

Supported layout checks: 100%, 150%, 200% display scaling, with logical window
sizes of at least 1360 x 900. Pixel density does not expand the supported playback
profile. Representative editing tests compare project content with direct typed
commands and verify unchanged live revisions during provisional movements.


## Measured results

[Recorded measurements and source hashes](timeline-evaluation-results.json) identify
the tested implementation. The isolated gesture benchmark uses one video track,
1,000 two-second clips separated by one-second gaps, one offline synthetic asset,
and no media cache. It measures command validation and visible drawing; it does not
claim OS input-to-display latency or simultaneous playback/decoding performance.

| Scale | Press ms | Median update ms | p95 update ms | Maximum update ms |
| --- | ---: | ---: | ---: | ---: |
| 100% | 0.376 | 2.414 | 2.933 | 4.801 |
| 150% | 0.368 | 3.880 | 5.959 | 6.748 |
| 200% | 0.434 | 6.896 | 9.791 | 12.487 |

All scales meet the original Release targets; none were relaxed. Debug measurements
are diagnostic only.

## Acceptance coverage

- `timeline-edit`: exact fractional-frame, playhead and edge snapping, either moving
  edge, trim extension/source bounds, overlap/track rejection, detached candidates,
  revision conflicts and undo/redo.
- `qt-timeline-{1,1.5,2}`: actual in-process mouse/key events, one release/one commit,
  unchanged snapshots during five intermediate movements, both trims, cross-track
  movement compared with a direct command, rejected overlap/off-track/zero-duration
  drops, Escape, external revision cancellation, keyboard selection/split/delete,
  fractional frames and the 1,000-clip measurement.
- `qt-timeline-workflow-{1,1.5,2}`: visible desktop controls assemble a 120-second
  reference containing ten base video clips, ten two-second B-roll inserts, ten
  dialogue clips and ten music clips. Generated signals stand in for dialogue/music.
  Source ranges, nine library drag/drops and direct-command equivalence, overlap,
  stale/abandoned media drags, track gain/mute/reorder, output-rate/dimensions,
  undo/redo, invalid source selection, preview and native save/reload are checked.
  The workflow uses in-process Widgets and generated media, with no OS GUI automation.
- `qt-media-cache`: 160x90 thumbnail content, 1,000 waveform bins against an independent
  PCM oracle (tolerance 0.00004), same-size modified source detection, relink invalidation,
  256-entry/16-MiB payload bounds, queue bounds, missing-source responses, an eight-second
  stuck child with a responsive UI heartbeat, and cancelled-generation rejection.
- Full Windows Debug **50/50**, Release **50/50**, independent default core **30/30**
  and Qt-free MCP with official SDK **33/33** passed. After final layout/label changes,
  the seven affected Release UI checks passed again. Warnings-as-errors builds and
  clang-format 19 checks passed.
- Windows runtime deployment succeeded. Both the cache test and two-minute reference
  workflow passed with Qt SDK locations removed from PATH and QT_PLUGIN_PATH.
  The optional translation-catalog deployment warning is unchanged; English UI and
  JPEG decoding were verified.
- Screenshots at 100%, 150%, 200% were visually inspected at the default 1360x900
  logical window size. Main inputs fit; the timeline scrolls lower tracks into view.
  The arrow-label padding correction was rechecked. Generated screenshots and native
  reference projects remain under ignored `build/timeline-*`; CI retains screenshots
  and performance JSON as artifacts.

## Reproduce

With the optional desktop and media integration enabled:

```text
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release --target format-check deploy-desktop
```

`deploy-desktop` is Windows-specific. CTest regenerates the synthetic media fixtures.
The detailed test/fixture definitions are in CMakeLists.txt.

No new dependency or native-format migration is introduced. The core/MCP command
boundary remains the editing authority. Cache TTL/source-identity limits, the
24-hour preview profile and unsupported advanced editing operations are documented
in [timeline editing](timeline-editing.md). Linux, macOS Apple Silicon and Intel,
and fresh Windows CI results have not been observed for this change. The milestone
is locally accepted; cross-platform completion remains pending.
