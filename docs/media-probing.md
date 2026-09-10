# Media probing and source metadata

Milestone 3 adds local media discovery, command-based import, verified original relinking,
and read-only source availability inspection. No graphical timeline, playback or frame
rendering is introduced. ffprobe may inspect codec headers while discovering streams.

## CLI

Install a trusted ffprobe executable on PATH, or supply its path as the final argument:

~~~powershell
.\build\Debug\editor-cli.exe new .\build\project.nle "Media example"
.\build\Debug\editor-cli.exe probe "D:\media\take.wav" "D:\tools\ffprobe.exe"
.\build\Debug\editor-cli.exe import .\build\project.nle "D:\media\take.wav" "D:\tools\ffprobe.exe"
.\build\Debug\editor-cli.exe media-status .\build\project.nle
.\build\Debug\editor-cli.exe relink .\build\project.nle 1 "D:\media\replacement.wav" "D:\tools\ffprobe.exe"
~~~

Use the media ID returned by import, rather than assuming 1. Linux/macOS use
./build/editor-cli with the same arguments. Windows CLI input uses wide arguments and
converts to UTF-8; stored native filesystem paths are UTF-8. Paths with spaces, Unicode
and shell metacharacters are arguments, never shell source. The final executable path
is optional. A missing backend reports an error rather than downloading software.

probe is read-only. import/relink load and validate the project, probe the source, apply
a command, and save only after success. Each CLI process owns its editing session;
there is no interprocess save conflict protection or persistent undo stack.

## Model and timing

SourceMetadata contains container name, probe version/build configuration, observed byte
size, optional container duration and 1–64 usable audio/video streams. Each stream records
codec, source-local index, exact time base, raw duration/start ticks when known, dimensions
or sample rate/channels, and average/nominal frame duration when reported.

Integer ticks are converted through checked rational arithmetic without floating seconds.
For example, 96,000 samples at 48,000 Hz are exactly 2 seconds; a reported average
30000/1001 fps becomes 1001/30000 seconds per frame. Negative source start ticks are
preserved separately from the nonnegative timeline. Unknown starts remain explicitly unknown.

The usable asset duration is the minimum of its audio/video stream durations. When a stream
has no duration ticks, the container decimal duration is used as an **estimate**, parsed
without binary floating point. Inspection labels each duration stream-ticks or
container-estimate. Missing both forms of duration rejects import. Container rounding is
not exact sample/frame evidence; overflow also rejects rather than silently rounding.

Attached pictures, subtitles and data streams are excluded from editable stream capability.
Average/nominal rates are metadata only: variable-frame-rate sample mapping is not implemented.
Stream start offsets do not yet implement A/V alignment. Current clip source ranges refer
to each stream's normalized beginning; the minimum duration policy ensures bounds under
that convention. Rotation, color/HDR, channel layouts, stream selection, edit-list semantics,
frame indexes, decoding validation and playback synchronization remain deferred.

## Relinking and availability

ProbeResult creates typed commands; it cannot mutate a live project. Callers probe outside
Editor, then submit with the revision captured before probing. Transactions, failed/stale
commit behavior, attribution, history limits and undo/redo use the existing command path.

ReplaceMediaSource preserves MediaId and all clip references, requires the same audio/video
kind, and validates every existing clip against the replacement duration. Short replacements
that invalidate clips are rejected atomically. This checks compatibility, not content identity:
a different recording of sufficient length and matching kind can be explicitly relinked.

RelinkMedia remains the unverified locator command from Milestone 2. Editing/removing its
original locator clears source metadata. Proxy locator edits preserve original metadata;
verified proxy generation/validation is deferred. Undo restores the previous source facts.

Availability is computed on demand: unlocated, available, missing, unavailable, or size-changed.
The status concerns the original only. Available means a regular file of the recorded size
(if recorded), not identical content or guaranteed future readability. Old relative locators
resolve against the caller's current directory. New probes store canonical absolute paths.
The backend checks file size and modification time before/after discovery to catch ordinary
concurrent modifications; this is not a content hash or a guarantee against replacement races.

## Bounded execution and supported scope

The adapter accepts local regular files, restricts FFmpeg protocols to file and demuxers to
wav,mov,matroska,avi,flac,mp3,ogg,aiff. This includes MP4 through mov and WebM through matroska,
subject to the installed backend's codec capabilities. Only the generated corpus below is
locally verified; the allowlist is not a promise of every codec/container combination.
Network URLs, streaming playlists and folders are outside scope.

Processes run without a shell, with null stdin, captured stdout/stderr and no visible window.
Default deadline is 30 seconds; captured output is limited to 1 MiB. C++ callers can cancel
with a stop token. Windows uses a job with kill-on-close and restricted handle inheritance;
POSIX uses spawn, a process group and termination/reaping on failure. Timeout, cancellation,
nonzero exit, malformed output and resource-limit errors publish no edit. This is process
supervision, not an OS security sandbox or a whole-process memory ceiling. Use trusted tools.
Probe input analysis is limited to 5 MB / 5 seconds of analysis, not a total input read limit.

## Validation and repeatable corpus

Default CTest adds parser, model/command and subprocess suites using no external backend.
For real-file tests, install ffmpeg/ffprobe and Python 3, then configure:

~~~sh
cmake -S . -B build -DNLE_MEDIA_INTEGRATION=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
~~~

Set NLE_FFPROBE_EXECUTABLE, NLE_FFMPEG_EXECUTABLE and Python3_EXECUTABLE explicitly when
needed. Enabling integration fails configuration if required tools are missing; default
builds remain independent of their installation. CI has separate required integration
jobs for Windows, Linux, macOS Apple Silicon and macOS Intel.

The corpus generator creates two-second 48 kHz stereo PCM WAV, a shorter WAV, fractional-rate
MPEG-4 video in MP4, 24 fps FFV1/PCM in Matroska, a Unicode filename copy, invalid data and a
network playlist. Tests cover timing, import, persistence, offline status, incompatible/short
relink rejection, stable clip IDs, undo/redo and unchanged project files on failed CLI edits.
Process fixtures cover literal argument handling, missing executable, nonzero exit, timeout,
midflight/preflight cancellation and output limits. Parser fixtures cover malformed sections,
integers, fractions, duplicate fields/indices, unavailable timing, overflow and attached art.

Local results and backend configuration: [media evaluation](media-evaluation.txt).
The backend decision and limits of the FFmpeg/GStreamer comparison are recorded in
[ADR 0009](adr/0009-media-probing.md).
