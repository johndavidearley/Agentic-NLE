# ADR 0003: Application-owned model and native text

Status: accepted for milestone 1. Date: 2026-09-09.

Own project/sequence/track/clip/media types express editor identity and invariants.
OpenTimelineIO is a future interchange adapter, not the authoritative document.
[OTIO describes an editorial interchange API and format](https://opentimelineio.readthedocs.io/en/latest/index.html).
Its fit for future history, effects, transactions and provenance is not established
here; this is a boundary decision, not a claim that OTIO cannot support those concerns.

Choose a documented, bounded, strictly validated text grammar using the standard library.
JSON with nlohmann/json would improve external tooling and schema familiarity but adds
a dependency not needed by this C++/CLI slice.
[nlohmann/json is MIT licensed](https://github.com/nlohmann/json/blob/develop/LICENSE.MIT).
Do not write a custom JSON parser. Revisit a mature schema format before integrations
depend on this representation.

See the native-format document for limits and migration policy. Review OTIO's exact
version, license and distribution requirements before adding a build dependency.
