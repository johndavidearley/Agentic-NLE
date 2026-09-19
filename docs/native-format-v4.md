# Native project format, version 4

The writer emits version 4. The reader accepts versions 1–4. Historical grammars remain
in [version 1](native-format-v1.md), [version 2](native-format-v2.md) and
[version 3](native-format-v3.md). Unknown versions reject.

Version 4 retains version 3 and adds a CLOCK record after each present source's METADATA
record, plus an estimated duration pair at the end of every STREAM record:

~~~text
SOURCE present
if present == 1:
  METADATA "container" "probe_version" "probe_configuration" byte_size container_value container_rate S
  CLOCK mode container_start_known [signed_start_value positive_start_rate]
  S * STREAM index kind "codec" time_base_value time_base_rate duration_ticks start_known start_ticks frame_value frame_rate nominal_value nominal_rate width height sample_rate channels estimate_value estimate_rate
~~~

CLOCK mode is legacy-per-stream=0 or shared-origin=1. The optional signed pair appears only
when container_start_known is 1. Stream start ticks are signed; durations, clip positions,
source ranges and other time pairs remain nonnegative exact rational seconds. Zero estimate
means unavailable. Flags, enums, rational arithmetic and existing record limits are validated.

## Shared source clock

For shared-origin assets, source zero is the earliest declared A/V stream start. Each stream
retains its exact offset from that origin, even when the origin is negative. Known stream
origins are required; a single audio stream with no reported start is treated as starting at
zero. Sources with ambiguous mixed/video origins retain legacy timing on import.

The source span is the maximum of stream offset plus stream duration, preserving delayed
starts and tails. Integer stream duration ticks take priority. When those are missing,
Matroska DURATION tags supply labelled per-stream estimates: the tag is an end timestamp,
so the stream's signed start is subtracted. Otherwise a container estimate is used for the
whole source span. Matroska's reported container end is normalized by the shared origin.
Other container duration estimates are spans. Missing positive duration information rejects.

The optional container start records the demuxer's origin separately. The preview adapter
uses it to map the shared clock to the player's clock; it does not redefine edit coordinates.
The logical asset kind/duration must agree with its metadata and retain an original locator.

## Migration and verified relink

Versions 1/2 load with absent metadata. Version 3 metadata receives legacy-per-stream mode,
no container-origin observation, and unavailable per-stream estimates. Its old minimum-span
rule is retained. Object IDs, allocation watermark, clip coordinates, project revisions and
audit records remain unchanged. No source is decoded or clock convention silently changed
during load. Saving writes version 4. There is no downgrade writer.

Verified relink preserves an existing asset's clock mode and source coordinates. A replacement
for a shared-origin asset must establish a shared clock. A legacy asset stays legacy even
when the replacement probe can establish a shared clock. New stream facts/duration still
undergo normal clip-bound validation; failure changes nothing. Undo/redo restores metadata.
Importing a file as a new asset opts into the newly probed convention. This avoids guessing
how old edits intended to align streams that the earlier player could not preview.

The 16 MiB file ceiling, 100,000 nested-record budget, 10,000 operation cap and 1,024-action
batch limit remain. Source headers are bounded by their owning asset; streams count as
records. Malformed input and trailing data reject. SourceTime rejects unrepresentable signed
magnitudes/intermediate arithmetic independently of the nonnegative edit-time type.

## Persistence boundaries

Frame indexes and temporary playback files are derived session caches and are never saved
as project locators. Online/offline status is recomputed. Metadata is a probe observation,
not a content hash. Unverified original-locator edits clear metadata as before.

Validation and serialization finish before atomic replacement. Undo stacks and transactions
remain session-local. Attribution is descriptive, not an authenticated replay journal.
Independent-writer conflicts, crash recovery and power-loss durability remain deferred.
