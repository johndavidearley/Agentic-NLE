# Native project format, version 1

ASCII keywords and decimal integers form a whitespace-separated grammar. Text is
conventionally UTF-8; bytes are preserved without Unicode validation. Writer output uses
LF. Strings require double quotes; backslash escapes the following character as in
C++ std::quoted, not JSON escape sequences. Text must contain 1–4096 bytes without
ASCII control characters or DEL.

N * record below means exactly N records. Times are pairs of signed 64-bit integers
representing nonnegative rational seconds, with positive rate.

~~~text
NLE_PROJECT 1
PROJECT project_id next_id "name"
MEDIA N
  N * ASSET media_id kind "name" duration_value duration_rate L
      L * LOCATION role "uri"
SEQUENCES N
  N * SEQUENCE sequence_id "name" frame_value frame_rate T
      T * TRACK track_id kind "name" C
          C * CLIP clip_id media_id position_value position_rate
                   source_start_value source_start_rate duration_value duration_rate
END
~~~

Records may span whitespace. The writer emits one record per line, with reduced time
fractions and deterministic bytes for a given snapshot.
Track kind: video=0, audio=1. Media kind: video=0, audio=1, audio/video=2.
Location role: original=0, proxy=1; at most one locator per role.

IDs/counts are unsigned 64-bit integers. Object IDs cannot be zero. The next_id
watermark exceeds all allocated IDs, including undone creations; it is not inferred
only from currently visible objects.

The reader rejects unknown versions/enums, invalid integers, missing quotes, truncation,
trailing data, duplicate IDs, invalid references, unsorted/overlapping clips, source-bound
violations, and time overflow. Limits are 16 MiB and 100,000 combined asset/location/
sequence/track/clip records. Saves enforce the same limits. Empty containers are valid.

Load returns a validated snapshot without modifying a session. History is not persisted.
Future versions require an explicit migration policy.

Save reserves an adjacent staging directory, writes/closes a full temporary file, then
replaces the target (MoveFileExW on Windows, filesystem rename on POSIX).
Validation/write failures do not truncate the destination. Concurrent-write detection,
fsync-based power-loss durability, symlink policy, journaling and crash recovery are
not implemented. Interrupted saves may leave .tmp-* staging directories.
