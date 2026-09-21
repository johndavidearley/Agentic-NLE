# Milestone 8 production playback evaluation

Windows local acceptance passed on 2026-09-18, including the ten-minute production run.
Milestone 8 platform CI passed on 2026-09-21; [separate platform evidence](desktop-ci-evaluation.md)
records Windows, Linux and both macOS architectures.
The [prototype comparison](multitrack-prototype.md) selected the backend before integration;
its measurements are separate from the production results below.

## Results

[Raw production results](multitrack-results.json) retain both twelve-second checks and the
600-second run. The fixed targets were set in the prototype before these measurements.

| Ten-minute production measurement | Result | Target |
| --- | ---: | ---: |
| Initial readiness | 457 ms | <= 3,000 ms |
| Seek readiness p95 | 579 ms | <= 1,000 ms |
| Cancellation | 7 ms | <= 250 ms |
| Audio sample error / discontinuities / unintended silent cut gaps | 0 / 0 / 0 | Zero |
| Frame-selection / black-gap errors | 0 / 0 | Zero; PTS within 2 us |
| Late presentation frames | 2 (0.0111%) | <= 1% |
| Underruns | 0 | Zero |
| A/V delivery drift | 0.498 ms | <= 20 ms |
| First-to-last presentation lateness drift | 0.276 ms | <= 20 ms |
| Peak queued PCM / prepared image bytes | 602,030 bytes | <= 64 MiB |
| Peak process-tree RSS | 118,321,152 bytes (112.84 MiB) | <= 1 GiB |

All 57,600,000 delivered stereo float samples matched the reference. The worker reported
17,984 images: 17,983 output-grid frames plus the readiness poster. Two images were late
at about 62 seconds; no frame-selection error or audio underrun accompanied them. The maximum
producer encode took 80.020 ms, absorbed by the prebuffer; the maximum clock tick gap was
49.127 ms. These occasional delays remain visible in the raw results rather than hidden.

The full Windows suites passed 42/42 in Debug and Release; the independent default core
passed 29/29 and the Qt-free MCP build passed 32/32. clang-format 19 and warnings-as-errors
passed. The deployed Windows desktop smoke test passed with SDK paths removed, and the
available-device check passed using silent PCM through the real QAudioSink. After the long
run, Qt boundary integer conversions were made explicit for LP64 Linux/macOS compilers;
these preserve Windows types and behavior. The affected playback/supervision tests passed
again on that final source in Debug and Release (4/4 each, including corpus setup).
The later [platform follow-up](desktop-ci-evaluation.md) passed all 28 CI jobs at 3323cea.

## Configuration and method

Host: Typhoon, Windows x64, AMD Ryzen 5 2600 (6 cores / 12 logical processors),
17,101,860,864 bytes installed RAM. MSVC 19.44 with warnings as errors, Qt 6.10.3,
Qt FFmpeg 7.1.3. The independent fixture/probe executable is FFmpeg 9.0.1.

The reference is a saved/reloaded ten-minute sequence made from twelve-second synthetic
sources: two 1920x1080 video tracks (30000/1001 MPEG-4 with B-frames and offset VFR FFV1),
four stereo 48 kHz PCM tracks, repeated source jumps/hard cuts, alternating top video,
intentional black and silent gaps, and gain that exercises hard clipping. Preview is 960x540.
This measures a long edited timeline, not seeking deep into a ten-minute source file.

The real SequenceTransport and nle-sequence-worker execute playback. The harness compares
all delivered stereo samples with independently generated WAV signals and all video PTS
with an external ffprobe frame index. Expected routing/cut positions are calculated by the
harness directly rather than by reusing the production sequence evaluator. Black-gap images
are also checked. Contiguous, exact PCM across cuts rules out unintended silent cut gaps.

Separate production-decoder checks cover CFR, VFR, B-frames, positive common origins,
relative delayed audio/video, negative origins, explicit audio stream selection, millisecond
packet timestamps, mono 44.1 kHz resampling, EOF flushing and repeated nonsequential seeks.
Offset/negative audio is compared with a separate FFmpeg-generated stereo PCM oracle.
Model tests cover detached plans, hidden embedded audio, enable/mute/gain, exact boundaries,
invalid command atomicity, undo/redo, routing through splits/moves and native v1-v4 migration.
The official MCP client exercises a 13-command batch covering the editing operations and
new playback settings, with schema validation, undo/redo and save/reopen.

Readiness includes worker creation, opening sources, a poster and 240 ms prebuffer, excluding
independent oracle preparation. Seek readiness uses ten fixed nonsequential requests and the
same lower order-statistic p95 method as the prototype. Stop measures process retirement.
Rapid replacements, reentrant cancellation, crashes, source changes and timeouts have separate
supervision checks; failed playback leaves the editor unchanged.

Timing uses silent output on one monotonic sample clock. For each video delivery, the
harness pairs the most recent PCM delivery at/before its timeline timestamp, subtracts their
timeline separation, and compares mean skew in early and late three-second windows. It also
records first-to-last video delivery lateness. These measure delivery drift, not physical
speaker/display synchronization. A separate available-device test sends silent PCM through
the real QAudioSink and checks its processed-sample clock.

The producer decodes and encodes preview JPEGs before queueing. The 32-chunk queue holds
at most 320 ms and has a 64 MiB hard limit. Queue bytes count retained PCM and encoded
image messages; decoder working frames are covered by process-tree RSS. RSS includes the
harness and children, sampled every 25 ms. Neither metric is an allocator-level proof.
No builds run alongside the long performance measurement.

## Debug timing correction

An initial full Debug run passed 41/42 tests but missed the fixed late-frame target:
7 of 361 frames were late (1.94%). Diagnostics identified up to 51 ms spent encoding an
image on the clock thread and a 95 ms timer gap. Encoding now runs on the existing producer
before enqueueing, so slow encoding consumes prebuffer rather than blocking audio feeding.
The complete corrected Debug suite passed 42/42; the playback run had zero late frames,
zero underruns and a maximum timer gap of 4.829 ms. The original targets were unchanged.

## Reproduction and limits

Build the optional desktop with NLE_MEDIA_INTEGRATION=ON and the dependency setup in
[desktop-preview.md](desktop-preview.md). Run CTest for the generated corpora and short
production checks. For the long test, the Python RSS wrapper additionally requires psutil:

~~~text
python tools/run_multitrack_acceptance.py <nle-multitrack-tests> <nle-sequence-worker> <multitrack-corpus> <ffprobe> <media-corpus> <Qt-prefix> <report.json> --seconds 600
~~~

The [playback contract](desktop-preview.md) defines the supported profile and failure bounds.
SDR, speed 1, hard cuts, top-video selection and stereo mixing are the measured scope.
Audio seeking decodes from the source origin with a five-second operation deadline;
unrestricted long-source seek performance is not claimed. Output timing, routing and source
coordinates are saved, while media availability and decoded buffers remain session state.
Export, effects, HDR, rotated-source normalization and display color management are outside
this milestone. Windows measurements do not establish a Linux/macOS platform pass.
