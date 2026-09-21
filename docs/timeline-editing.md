# Practical timeline editing

Milestone 9 adds timeline gestures and derived media views to the existing command-based
desktop. Native version 5 is unchanged. Projects do not depend on an open chat, desktop
session, thumbnail cache or waveform cache.

## Editing

Select imported media and set **Source in** and **Source out** in seconds, decimals or
exact fractions. Append uses that range. **Place on track...** creates a track or selects
an existing destination. Drag from the media library onto an existing track to place the
selected source range at the pointer. Audio-track placement disables video routing.
Creating a track still uses the Place dialog.

Drag a clip body to move it, including between compatible tracks. Drag either edge to
trim it; a left trim preserves the right edge and advances or reveals source media.
Overlaps and unavailable source handles are rejected. The ghost shows a provisional
result; its outline turns red for an invalid target and an explanation appears below.
Release commits one command. Escape cancels. A project/revision change cancels an
unfinished gesture. Undo/redo treats the complete gesture as one edit. The inspector
continues to accept precise numeric positions, source in and duration.

**Snap** attracts either moving edge, or the trimmed edge, to the nearest eligible frame,
playhead or other clip boundary within eight logical pixels. Exact equal-distance ties
prefer the playhead, then another clip boundary, then a frame. Snapping uses exact
rational frame times, including 30000/1001; it never changes source metadata. Disable
Snap for arbitrary positions. Pointer positions without a snap use microsecond
resolution; use the inspector for finer exact fractions.

Zoom changes the time scale independently of sequence length. Use the slider, **+/-**
or **Ctrl+wheel** (anchored at the pointer). **Shift+wheel** scrolls horizontally; the
normal wheel and vertical scrollbar scroll tracks. Horizontal scrolling also follows
a dragged pointer near the viewport edge. The initial empty extent is two minutes,
with space beyond the last clip for placement, bounded to the 24-hour preview profile.

Track headers expose **On**, **Mute**, gain (1000 is unity; range 0-4000), and up/down
reordering. These are ordinary undoable commands. **Sequence settings...** changes
even output dimensions up to 3840x2160 and an exact frame rate between 1 and 60 fps;
audio remains stereo 48 kHz. Output settings do not retime existing source ranges.

With the timeline focused:

| Key | Action |
| --- | --- |
| [ / ] | Previous / next clip, scrolling it into view |
| S | Split selected clip at the playhead |
| Delete | Delete selected clip |
| Space | Play / pause |
| Left / Right | Previous / next sequence frame |
| Home | Seek and scroll to the beginning |
| + / - | Zoom in / out |
| Escape | Cancel a provisional gesture |
| Ctrl+Z / Ctrl+Y | Undo / redo through the desktop shortcuts |

macOS uses the platform-standard Command-based undo/redo shortcuts.
Tab navigates the native controls. The current layout is checked at 100%, 150% and 200%
scaling with a 1360x900 or larger logical desktop. Smaller work areas are unmeasured.
The existing nonoverlap rules remain: no ripple, roll, slip, slide, linked groups or
multi-clip transforms.

## Derived media cache

Visible clips request 160x90 thumbnails in two-second source buckets and waveforms in
ten-second source blocks. A waveform bin is the maximum absolute stereo sample over
10 ms of decoded 48 kHz audio, clipped to full scale; the display applies track gain.
Source timing, stream routing and origins come from the existing decoder. Thumbnails
and waveforms are disposable UI data, never stored in the native project or used as
export inputs.

Two asynchronous supervised child workers decode at most two requests at once.
The waiting queue holds at most 32 requests. An LRU retains at most 256 entries and
16 MiB of image/sample/string payload; Qt/container overhead and worker memory are
additional. Each child has an eight-second timeout and a 512 KiB response limit.
New project/media metadata invalidates the cache generation and cancels old work.
Edits that leave media identity unchanged reuse cached media.

Identity includes project/media metadata, original path, byte size, modification time,
stream, media type and exact source bucket. Filesystem checks and decoding run outside
the UI thread. Successful entries are rechecked after one second when requested;
failed entries retry after five seconds. An old image can remain during revalidation.
Size/mtime is not a content hash: undetectable same-size/same-time changes require
reimport/relink. Relink invalidates cached results. Missing files, unsupported sources,
decoder deadlines and failed workers leave the project editable with blank derived
views. Long-source audio cache requests share the playback decoder's seek limits.

The implementation adds no external dependency. The desktop deployment includes
nle-cache-worker and uses the already selected Qt/FFmpeg runtime and licensing policy.
See [acceptance evidence](timeline-evaluation.md).
