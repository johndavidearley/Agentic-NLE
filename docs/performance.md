# Milestone 2 performance and validation

Measured 2026-09-10 on Windows x64, AMD Ryzen 5 2600, MSVC 19.44, Release build.
These are single local observations, not statistical performance guarantees.

## Workload and budgets

The deterministic fixture contains one sequence, one video track, one logical asset,
and 1,000 or 10,000 nonoverlapping one-second clips separated by gaps.
A transaction moves the first 100 clips by half a second, commits once, then serializes
and deserializes the resulting snapshot and checks equality.

Initial acceptance budgets for the 10,000-clip Release workload are: stage 100 edits
within 1 second, commit within 50 ms, serialization/deserialization each within 250 ms,
and retain less than 2 MiB of accounted undo payload for the batch. These are local
engineering targets, not timing assertions in shared-runner CI.

| Clips | Stage 100 edits (ms) | Commit (ms) | Undo payload (bytes) | Serialize (ms) | Deserialize (ms) |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 41.871 | 0.303 | 129,136 | 4.557 | 1.451 |
| 10,000 | 427.335 | 2.867 | 1,281,136 | 43.380 | 13.721 |

All measured budgets pass. Staging still copies and validates the whole project per
command, so it scales with batch length and project size. Serialization includes a
validation reload. History excludes duplicated audit logs. Payload accounting is not RSS:
it excludes allocator/bookkeeping overhead and temporary/live snapshots. Large audit logs,
more media/tracks, concurrent contention and million-clip timelines are not characterized.

## Reproduce

~~~powershell
cmake -S . -B build -DNLE_BUILD_BENCHMARKS=ON
cmake --build build --config Release
.\build\Release\nle-benchmark.exe
~~~

Linux uses ./build/nle-benchmark. Benchmark invariants fail the executable; elapsed time
is reported as diagnostic data. No external media, random timing input or downloads are used.

## Verification scope

Windows Debug and Release builds use warnings as errors. The 22 CTest entries include
the original timeline regression suite, transactions, explicit/destructor rollback,
poisoned batches, moved/foreign/expired handles, stale/ABA revisions, competing threads,
entry/byte budgets, track/media edits, migration and persisted attribution.

The parser suite mutates 2,000 inputs with a fixed seed: malformed documents must reject,
and accepted documents must validate and round-trip. Existing truncation, integer-bound,
invalid-ID/source-range, Unicode-path and oversized-file tests remain enabled.
A Windows sharing-lock test proves a failed save keeps the prior file and removes staging.
These tests do not simulate power loss or exhaustively test memory-allocation failures.

The transaction CLI demo builds a sequence in seven staged commands, previews it,
commits as one entry, undoes/redoes and verifies save/load.
clang-format 19 checks the sources, tests and benchmark.

CI configuration covers GCC and Clang on Ubuntu, MSVC on Windows, Debug/Release,
and Linux ASan/UBSan. Hosted CI and Linux sanitizer results have not been run or
claimed from this Windows session. No dependencies or codecs were introduced.