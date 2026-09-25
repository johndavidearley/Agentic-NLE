# Export a sequence

Milestone 10 export is in progress. The optional Qt/FFmpeg build provides a lossless reference
file and an MP4 delivery preset. Bounded Windows synthetic media acceptance and a manual desktop
workflow have passed; the remaining platform and failure-case gaps are recorded in
[the evaluation](export-evaluation.md),
[Milestone 10](next-milestones.md) and [ADR 0015](adr/0015-local-export.md).

## Desktop

Build and deploy the optional desktop targets using the steps in [desktop setup](desktop-preview.md#build).
In the editor, choose **Export…**, select the Matroska lossless-reference or MP4 H.264 delivery
preset, and choose a destination. The progress dialog reports the captured project revision,
frame count and audio samples. Cancel stops the job and removes its staged output. The export
uses a detached snapshot, so later edits do not change the running export.

## Headless

Build with the same optional desktop/FFmpeg configuration, then inspect the project to find its
sequence ID:

~~~powershell
.\build\Release\editor-cli.exe inspect .\project.nle
.\build\Release\editor-export.exe .\project.nle 1 D:\renders\reference.mkv
.\build\Release\editor-export.exe .\project.nle 1 D:\renders\delivery.mp4
~~~

The sequence ID is the positive integer shown after `Sequence` in inspect output. An `.mkv`
file selects the lossless reference preset; `.mp4` selects H.264/AAC. Progress is written to
stderr about once a second. Ctrl+C requests cancellation. To explicitly replace an existing
file, add `--overwrite`; otherwise an existing destination is preserved and the export fails.
The destination folder must already exist.

On Linux/macOS, use the corresponding `./build/editor-cli` and `./build/editor-export` paths.
The runtime FFmpeg libraries must be available to the executable. Encoder availability varies
by FFmpeg build; missing required encoders are reported before output installation.

## Supported profile and limits

- Output uses the sequence’s even width and height (up to 3840×2160), frame rate (1–60 fps),
  and 48 kHz stereo audio settings.
- Export reads original sources at full output resolution, with source media limited to
  1920×1080. Video is aspect-preserving, centered and letterboxed with black. Gaps render black
  and have silent audio unless other enabled tracks contribute.
- Frames are emitted on the output frame grid while their timestamps are before sequence end.
  The renderer produces ceil(duration × 48000) PCM samples. The reference preserves them, and MP4
  AAC stores end-padding metadata so its decoded sample count matches exactly. Local acceptance
  verified 1,023, 1,024 and 1,025 sample outputs. See [evaluation](export-evaluation.md).
- The export profile is SDR BT.709. Unspecified source color tags are assumed BT.709; explicit
  non-BT.709 primaries, transfer functions or matrices, HDR, and rotated media are rejected.
  Effects and transitions are not rendered.
- The reference preset writes Matroska with FFV1 BGR0 video and 48 kHz stereo float PCM. The MP4
  preset uses libx264 CRF 18 (medium) when available; Windows falls back to Media Foundation
  `h264_mf` quality 85. Other platforms currently require libx264. MP4 audio is AAC-LC at
  192 kbit/s. Required encoders must be present in the linked FFmpeg runtime.

Export writes to a unique sibling staging directory. It checks source availability/change during
render and before installation. Cancellation or failure leaves an existing destination untouched;
replacement is installed atomically only after a successful job and explicit overwrite selection.
