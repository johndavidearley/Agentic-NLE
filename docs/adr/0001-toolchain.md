# ADR 0001: C++20, CMake and no bootstrap library dependencies

Status: accepted for milestone 1. Date: 2026-09-09.

C++20 provides value semantics, RAII, variants and strong types, with direct integration
paths to Qt and media libraries. CMake 3.24 supports Windows/Linux builds. MSVC 2022
is locally available. Warnings are errors; clang-format 19 is a development tool.

Rust offers stronger memory-safety defaults but adds FFI work for the preferred stack.
Python suits tooling but is not selected for the core. No performance comparison is claimed.

CTest runs a small explicit-check executable with named suites.
Catch2 (Boost Software License 1.0) and GoogleTest (BSD-3-Clause) offer richer fixtures
and diagnostics; the initial suite does not yet justify a downloaded dependency.
Adopt one when richer fixtures/parameterization become useful; do not grow the check
helper into a framework. Checks stay active in Release.
Sources: [Catch2 license](https://github.com/catchorg/Catch2/blob/devel/LICENSE.txt),
[GoogleTest license](https://github.com/google/googletest/blob/main/LICENSE).

CI config targets Windows/Linux Debug/Release. Original source uses MIT for permissive
open-source reuse. Each future dependency retains its own licensing and distribution
requirements; this does not settle future desktop binary licensing.
