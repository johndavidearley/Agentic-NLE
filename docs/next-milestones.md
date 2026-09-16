# Next development phase: a usable editor for people and agents

Proposed 2026-09-15, following Milestone 6 at commit 30f3024. Milestone 7 completed on
2026-09-16; Milestones 8–13 remain planned. This plan recommends completing the editing workflow first.
It defines deliverables and acceptance evidence, not calendar estimates or new backend choices.

## Phase outcome and order

A person can make a two-to-ten-minute video with B-roll, dialogue and music, refine it on a
visual timeline, recover interrupted work and export a playable file. An agent can prepare
media and propose the same edits, and the person can review and continue in the same project.
The saved project remains useful without the chat, agent session or running desktop.

| Milestone | User outcome | Depends on |
| --- | --- | --- |
| 7. Reliable projects and recovery | Reopen work after interruption and trust saves on supported platforms | 6 |
| 8. Multi-track playback | Preview B-roll, dialogue and music together | 7 |
| 9. Practical timeline editing | Arrange and trim clips directly, with zoom, snapping and audio waveforms | 8 |
| 10. Export a finished video | Produce a file that matches the supported timeline | 8; 9 for the full user workflow |
| 11. Agent media preparation | Import and relink approved media through MCP | 7 and 10's media/job contracts |
| 12. Shared human and agent editing | Review agent edits in the open desktop project | 9 and 11 |
| 13. Installable alpha | Run and validate the complete workflow on Windows, Linux and both Mac architectures | 7–12 |

Milestone 10 is the first complete human editing workflow. Milestone 12 completes the local
human/agent workflow. Milestone 13 packages that scope for repeatable evaluation. Numbering
expresses the recommended order; implementation still begins one milestone at a time.

## Milestone 7 — Reliable Projects and Recovery (complete)

[Recorded validation](recovery-evaluation.md): all 28 platform CI jobs passed on `b2978a0`.

**Outcome:** saved files and recent desktop work survive ordinary application interruptions,
with an explicit recovery choice and dependable Windows/Linux/macOS validation.

Deliver:

- Run the existing core, media, desktop and MCP CI jobs on the milestone revision. Investigate
  the last recorded Linux Qt dependency failure and Apple Silicon playback timeout; retain
  precision assertions and report any platform limitation explicitly.
- Reuse a shared document-save service for desktop, CLI writes and MCP. Cooperative writer
  ownership and external-change checks prevent these applications from unknowingly replacing
  each other's work. Define Save As and project-copy identity behavior using the existing policy.
- Add bounded desktop recovery checkpoints, separate from the user's saved file, with project
  identity, revision, validation and a recover/discard choice on reopening. Preserve the prior
  valid checkpoint during replacement; show recovery failures without losing the live document.
- Make MCP recovery an explicit launch policy. Existing read-only/edit/save permissions and
  default discard-on-close semantics remain meaningful. Document session expiry and unsaved
  work clearly; recovering state does not recover open proposals, undo stacks or retry results.

**Acceptance:** demonstrate process termination after a completed checkpoint, successful
recovery, recovery after a truncated newest checkpoint, missing-source reopening, rejected
conflicting writes, Unicode paths and permission/disk-write failures. Record actual platform
CI results. Checkpoints bound the possible loss window; do not claim power-loss durability.

**Boundaries:** one authoritative writer per project; no distributed merge or executable audit
journal. Fixes and recovery are separate reviewable slices within this milestone.

## Milestone 8 — Multi-track Playback

**Outcome:** B-roll can cover a video while dialogue and music continue on separate tracks.

Deliver in order:

1. Evaluate a bounded playback prototype before expanding the backend. Exercise two video and
   four audio tracks, source origins, VFR, fractional rates, rapid seeks, cuts and cancellation.
   Measure the current worker approach and one viable decoded-frame/audio pipeline alternative.
   Record the selected backend, exact dependencies, licenses and packaging consequences in an ADR.
   A failing prototype triggers a revised scope before the production implementation.
2. Extend the plain C++ sequence evaluation plan to report active clips, source ranges, video
   priority and audio contributions at exact timeline positions. The top enabled video wins;
   uncovered regions are black. Enabled audio contributions mix independently of video coverage.
3. Add persisted sequence output settings and explicit stream routing, track enable/mute and
   gain semantics. Preserve the existing video-with-embedded-audio behavior during migration;
   make independent audio placement deliberate so the same source is not duplicated by accident.
   New state changes use typed, undoable commands and matching MCP schemas.
4. Implement one coordinated playback clock, bounded decoded queues and cut preloading. Support
   stereo mixing and explicit gain/clipping behavior. Invalidate obsolete output on seek/edit.
   Retain process supervision and source-change detection.

**Acceptance:** play a ten-minute reference sequence with two video/four audio tracks, including
cuts, intentional gaps, 30000/1001 video, VFR and offset sources. Verify active-frame selection
and mixed audio against synthetic reference signals, plus seek/stop behavior, memory bounds and
native migration. Record dropped frames, underruns, cut gaps and A/V drift on named hardware.
Set and publish numerical targets during the prototype; completion requires meeting them or an
explicitly reduced supported profile. Timing thresholds must not be weakened to hide failures.

**Boundaries:** SDR, speed 1, hard cuts, top-video selection and stereo audio. Initial benchmark
profile: 1080p sources at 30 or 30000/1001 fps, with reduced-resolution preview permitted. Source
routing, scaling/letterboxing and output timing must be specified consistently for later export.
No transparency, transitions, effects, HDR or physical speaker/display synchronization claim.

## Milestone 9 — Practical Timeline Editing

**Outcome:** a person can assemble and refine a short video without entering every edit as numbers.

Deliver:

- Zoom and scroll independently of sequence length, with a readable ruler, track headers,
  playhead, selection and visible mute/enable/gain controls.
- Place media at a chosen track/time; drag one clip; trim either edge; split, delete and reorder
  tracks; use keyboard shortcuts and an optional snapping mode for frames, playhead and clip edges.
- Show a provisional gesture result and commit once on release. Escape cancels; invalid drops
  explain the problem. A changed project revision cancels a stale gesture. Undo reverses the
  complete gesture and precise numeric editing remains available.
- Add bounded thumbnail and waveform caches built outside the UI thread and keyed by source
  identity. Cache failure or absence must not prevent editing or change saved source timing.
- Expose source in/out selection and sequence output settings in the existing desktop controls.

**Acceptance:** assemble a two-minute reference edit with B-roll, dialogue and music using the
visible controls; compare representative gesture results with direct core commands. Exercise
snapping at fractional frame rates, overlapping/invalid drops, cancellation and undo/redo.
Record UI responsiveness on a 1,000-clip synthetic project and verify keyboard access and layout
at supported display scales. Commit no edit for intermediate mouse movements.

**Boundaries:** existing nonoverlap rules stay in force. Ripple/roll/slip/slide editing,
linked-clip groups, general multi-selection transforms and a node/effects UI remain separate work.

## Milestone 10 — Export a Finished Video

**Outcome:** export the supported sequence as a playable delivery file that agrees with preview.

Deliver:

- Render from a fixed project revision using the same sequence evaluation semantics as playback,
  with full-resolution decoded media and the original sources. UI preview images and reduced
  preview caches are not export inputs.
- Define output width/height, frame rate, SDR color assumptions, letterboxing and stereo sample
  rate. Specify frame/sample rounding, end-of-sequence behavior and unsupported-input rejection.
- Establish a lossless reference export, then one tested delivery preset targeting MP4, with the
  codec/build selection recorded before adoption. Keep encoder/backend dependencies optional.
- Add desktop and headless export entry points with progress, cancellation and bounded workers.
  Preflight missing/changed sources and unsupported features. Stage output separately; preserve
  an existing destination on failure/cancel and require an explicit overwrite choice.
- Permit editing while exporting only when the export remains bound to its original revision and
  source identities. Do not silently include newer changes in an in-progress render.

**Acceptance:** decode and inspect the result independently: exact intended frame count, duration,
cut/gap positions, source-origin alignment and reference audio boundaries. Compare reference
frames/audio before lossy encoding; use stated tolerances for the delivery preset. Repeat after
save/reload, and test cancellation, unavailable encoders, disk failure and changed media.
A person completes import → edit → preview → export → play the exported file.

**Boundaries:** no render farm, proxy-quality final exports, broad codec presets, effects,
transitions or cross-encoder byte-identical output promise. Preview/export agreement covers the
supported profile, with approximation and color limitations stated.

## Milestone 11 — Agent Media Preparation

**Outcome:** an agent can prepare a project using user-selected media and repair moved sources.

Deliver:

- Add optional MCP probing/import and verified relink through the existing media/command layer.
  The launcher selects media roots or approved assets and a fixed probe executable. Require a
  distinct media-access permission; track edits alone do not grant filesystem access.
- Model probing as a bounded cancellable operation outside the Editor lock. Return inspected
  facts and a proposal; apply accepted metadata with expected revisions and normal undo/redo.
- Give pending operations stable session-scoped IDs, progress/status and retry behavior. A
  cancelled or stale probe must not register an asset later. Extend discovery only for supported
  operations, and preserve the lightweight inspection/editing server configuration.
- Expose the implemented export job through a separately granted output destination policy.
  Use the same fixed-revision render path and observable result as desktop export.

**Acceptance:** with the official MCP client, import approved files into a launcher-created empty
project, build a rough cut, relink a moved asset, preview/commit, save and export. Reopen in the
desktop and compare contents. Cover denied paths, paths escaping approved roots, Unicode names,
missing/changed files, cancelled jobs, stale commits and uncertain retries without duplicate edits.

**Boundaries:** no arbitrary shell, whole-disk discovery, downloads, project search across the
machine, transcription, semantic media selection or remote MCP transport. Do not imply that local
path restrictions are an operating-system sandbox.

## Milestone 12 — Shared Human and Agent Editing

**Outcome:** the editor stays open while a local agent proposes changes that the person can see,
inspect, keep or undo.

Deliver:

- Keep one authoritative Editor for an open project. Add a small local session service or bridge
  so the desktop and MCP adapter share that authority. Choose ownership/lifetime/IPC after a
  focused prototype; preserve the independent headless mode and reuse the existing wire tools.
- Show pending agent proposals and a readable before/after change summary, revision and actor.
  Keep acceptance behavior tied to the configured permissions; review must not require a second
  independently loaded copy of the project.
- Refresh the desktop from committed snapshots. Human changes invalidate conflicting agent
  previews, and agent changes invalidate stale human gestures. Keep one ordered undo/redo history
  with actor attribution and clear behavior after client disconnect/reconnect.
- Serialize save and recovery through the owning session. Give outstanding media/export jobs an
  explicit owner and disconnect policy; a reconnect must not silently replay an uncertain edit.

**Acceptance:** a person trims while an agent proposal is pending; the stale proposal rejects.
A fresh agent batch appears in the desktop, commits once and undoes as one operation. Test
reconnects, duplicate requests, desktop closure and worker failure. Save, close both clients and
reopen the same project with stable IDs, timing and attribution and no chat/session dependency.

**Boundaries:** one local project authority, no distributed collaboration, branch merging,
remote authentication, durable retry replay or restoration of open transactions after a crash.

## Milestone 13 — Installable Alpha

**Outcome:** a tester can install and use the complete supported workflow without a development SDK.

Deliver:

- Produce versioned artifacts for Windows, Linux, macOS Apple Silicon and macOS Intel, with an
  explicit OS/architecture matrix, dependency notices and reproducible packaging instructions.
- Verify required runtime components and default tool locations in clean environments. Define
  signing/notarization requirements and credentials as release prerequisites; do not claim a
  signed public release merely because development packages build.
- Provide a redistributable sample project with generated media, a short getting-started guide,
  MCP connection examples and a plain-language supported-features/known-limits page.
- Run the complete human and agent workflow, interruption recovery and export from each packaged
  build. Measure the documented workloads and expose actionable messages for cache/history/audit/
  request limits. Fix blockers before labeling that platform's artifact supported.

**Acceptance:** a fresh tester creates or opens the sample, imports, edits across tracks, uses
an agent proposal, recovers interrupted work and exports a validated result. Record platform,
artifact version and evidence. Keep public publication/signing as a separate explicit release
step; this milestone prepares and verifies the artifacts.

## Planning rules and later work

At the start of each milestone, confirm the preceding acceptance evidence and name a small set
of implementation slices. At completion, update the roadmap, retain a concise evaluation and
record known limitations. Do not infer a platform pass from another platform's result. Continue
warnings-as-errors, formatting, relevant command/migration tests and the dependency-free default
core. Changes to persistent settings require a migration decision and fixtures. Backend choices
remain outside the project model, and new commands must work for human and agent clients.

The last recorded baseline is [MCP validation](mcp-evaluation.md); its platform observations are
historical evidence, not a fresh CI status check. [Precision validation](precision-evaluation.md)
and [performance](performance.md) define earlier measurements to preserve or explicitly replace.

After this phase, prioritize from real alpha use: interchange with explicit loss reporting and
identity remapping; proxy generation and larger projects; ripple/linked editing; titles and
captions; basic transitions and color tools. Audit compaction and history storage changes need
measured workloads and compatibility designs. Multicam, broad effects, advanced audio, AI media
generation, cloud collaboration and a plugin marketplace remain beyond this plan. Interchange
continues to be an adapter; native projects retain the authoritative editable state.
