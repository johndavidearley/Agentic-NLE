# Native project format, version 3

The writer emits version 3. The reader explicitly accepts versions 1, 2 and 3.
The earlier grammars are preserved in [version 1](native-format-v1.md) and
[version 2](native-format-v2.md). Unknown versions fail.

Version 3 retains the complete version 2 grammar and adds a SOURCE block after
**each asset's LOCATION records**, including assets with no probed metadata:

~~~text
NLE_PROJECT 3
PROJECT project_id next_id "name"
MEDIA N
  N * ASSET id kind "name" duration_value duration_rate L
      L * LOCATION role "uri"
      SOURCE present
      if present == 1:
        METADATA "container" "probe_version" "probe_configuration" byte_size container_value container_rate S
        S * STREAM index kind "codec" time_base_value time_base_rate duration_ticks start_known start_ticks frame_value frame_rate nominal_value nominal_rate width height sample_rate channels
SEQUENCES ...
AUDIT ...
END
~~~

Flags present/start_known must be 0 or 1. Stream kind is video=0 or audio=1.
Stream indices are unique source-local unsigned 32-bit descriptors, not project IDs.
All time pairs are nonnegative rational seconds with a positive denominator.
Unknown average/nominal frame duration or container duration is 0/1.
The frame pairs represent seconds per frame, not frames per second.
Duration ticks are positive signed 64-bit integers or -1 for unavailable.
Start ticks are signed 64-bit integers; if start_known is 0 they must be zero.

A source contains 1–64 usable audio/video streams and a positive byte size.
Video requires nonzero width/height and zero audio fields. Audio requires nonzero
sample rate/channels and zero dimensions/frame-duration fields. Text uses the
existing nonempty, 4096-byte, quoted, control-free grammar.

The effective duration is the minimum duration across usable streams, with a
container-duration estimate substituted only where stream duration is unavailable.
Integer stream duration is duration_ticks * time_base with checked, reduced arithmetic.
Every effective stream duration must be positive. The logical asset kind/duration
must match its source metadata, and an original locator must exist.
See [media probing](media-probing.md) for precision and stream-start limitations.

## Migration and limits

Versions 1 and 2 load with absent source metadata. IDs, allocation watermark, timing,
locators and ordering survive unchanged. Version 2 attribution is preserved; version 1
starts at revision zero without invented history. Saving either emits version 3.
There is no downgrade writer. Missing metadata in version 3 is represented by SOURCE 0,
not by omitting a required record.

The 16 MiB file ceiling and 100,000-record budget remain. Streams participate in the
record budget alongside assets, locations, sequences, tracks, clips, operations and
actions. The source presence/metadata header is bounded by its owning asset rather
than separately counted. The 10,000-operation and 1,024-action limits remain.
Malformed flags, enums, counts, durations, inconsistent metadata and trailing data fail.

Status is checked against the filesystem at inspection time and is not persisted as
an online/offline bit. Metadata is a historical probe observation, not a file hash.
Commands and undo/redo preserve it; an unverified original-locator edit clears it.

## Save semantics

Validation and serialization finish before an adjacent temporary file replaces the
destination. Failed probing, invalid relinks and malformed metadata cannot replace a
saved project. The reader is also used to enforce the writer's record budget.

Undo stacks and open transactions remain session-local. Audit entries are descriptive,
not authenticated replay instructions. Independent-writer save conflicts, crash recovery
and power-loss durability remain outside the contract.
