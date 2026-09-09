# ADR 0002: Exact reduced rational time

Status: accepted with limits. Date: 2026-09-09.

Use normalized signed 64-bit value/rate pairs for nonnegative seconds, rate positive.
A 24000/1001 fps frame is 1001/24000 seconds; a 48000 Hz sample is 1/48000.
Sequence frame duration independently preserves the intended frame grid.

Floating seconds introduce rounding/equality errors; a fixed tick rate constrains
frame/sample combinations. Rational time avoids both. Comparisons use continued
fractions without large cross products. Arithmetic checks all products and sums.

Overflow fails explicitly. Some representable final results still fail because intermediate
values exceed int64. Nonnegative values cover current source/timeline positions and
durations. Signed offsets, wider arithmetic, timecode, quantization and speed maps
need later decisions. Tests cover frame/sample fractions, invalid values, large
comparisons, overflow, and exhaustive small-fraction reference arithmetic.
