# Export evaluation

Recorded 2026-09-22. This is bounded Windows development-machine evidence, not a platform-wide
support claim. The export used the pinned Qt 6.10.3 FFmpeg 7.1.3 runtime; independent decode and
probe used FFmpeg/ffprobe 9.0.1. The linked runtime had FFV1, AAC and Windows Media Foundation
H.264, but no libx264. The MP4 therefore records `h264_mf quality 85`.

The generated native project was saved and exported at revision 9. Its 320×180, 24 fps, 2.5 s
timeline contains 60 frames: a video section at the beginning, a one-second black gap, and a
second clip beginning at source time 0.25 s. Stereo 48 kHz PCM plays for 2 s, followed by 0.5 s
of silence. The tests use generated local media, not user footage.

Independent ffprobe and decode checks passed:

- Both containers decode to 60 video frames and 120,000 stereo samples per channel at 2.5 s.
  Matroska metadata and MP4 metadata include revision 9, sequence 3, and the selected BT.709 SDR
  profile; the MP4 identifies `h264_mf`.
- Lossless reference frames at the two source origins match independently generated expected
  frames byte-for-byte; frames 24–47 are entirely black. The decoded float PCM matches the
  generated WAV samples exactly, then remains silent through the sequence end.
- MP4 decoded RGB versus reference RGB has mean absolute error 0.9194 on 8-bit channel values
  and maximum absolute error 34. AAC decoded PCM versus reference has mean absolute error
  0.001797 and maximum absolute error 0.19382. These are measurements for this synthetic signal,
  not universal quality guarantees.
- Separate AAC outputs at 1,023, 1,024 and 1,025 input samples decode to exactly those sample
  counts, including non-frame-aligned endings.
- An existing destination without explicit overwrite is rejected and preserved. Ctrl+C during a
  long render and a source-file change during a long render both abort, preserve the existing
  destination and remove staging. Missing-source export fails without leaving a destination or
  staging directory. A locked destination makes final installation fail while preserving the
  original bytes and cleaning staging.

## Manual desktop workflow — 2026-09-23

The deployed Windows Release editor completed import → edit → preview → save/reopen → export →
playback with generated `build/media-corpus/long-av.mkv`. The local ffprobe executable was
selected, the 12-second A/V source imported and appended, and the clip inspector used to set
timeline position 1 s, source in 1 s and duration 8 s. Undo restored the original clip; redo
restored the edit. Preview crossed the one-second opening gap and displayed later source frames.
The native project reopened at revision 5 with the edit intact.

The desktop exported a lossless reference before reload and an MP4 delivery after reload into
`build/manual-m10-20260923/`. Both progress dialogs reported revision 5, 216 frames and 432,000
audio samples. The reference reported FFV1/float PCM; MP4 reported `h264_mf`/AAC. Independent
FFprobe 9.0.1 decoded both as 1920×1080 at 24 fps, with 216 frames, 432,000 stereo samples per
channel and a 9.000 s container duration. FFmpeg black detection found video black from 0–1 s
in both. Silence detection found 0–1.000021 s in the reference and 0–0.999271 s in MP4; the
MP4 AAC boundary differs by 0.729 ms. FFplay 9.0.1 displayed advancing frames from the MP4.
This confirms visual playback and independent audio decoding, but does not measure physical
speaker output. The synthetic source carries a 0.5-second audio offset that precedes the
selected source in point, so the intended opening silence is one second.

The Windows Release build used warnings as errors. `sequence-evaluation`,
`qt-timeline-workflow-1`, `qt-desktop` and `qt-playback` passed (4/4) with existing local media
fixtures. This manual check adds workflow evidence; the earlier synthetic acceptance above
remains the measured frame/audio comparison.

For a bounded disk-full check, an 8 MiB Linux tmpfs was mounted temporarily under WSL Ubuntu
24.04 and accessed by the same Windows Release exporter over `\\wsl.localhost`. Exporting the
reopened revision-5 project to a new reference file failed while writing an encoded packet with
`No space left on device` (exit 1, after 24 of 216 frames). No final output or staging directory
remained. Repeating with `--overwrite` against an existing 2,017-byte destination produced the
same error; its SHA-256 hash was unchanged, and staging was again removed. The temporary mount
was unmounted and removed. This tests actual capacity exhaustion in the output filesystem, not
a simulated encoder or permission error.

Still outstanding: an unavailable-encoder run and export-enabled Linux/macOS builds and
acceptance. The CI platform matrix for the rest of the application is not export acceptance.
