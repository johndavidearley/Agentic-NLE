# Architecture decisions

Recorded 2026-09-09.

| ADR | Status |
| --- | --- |
| [0001 Toolchain, tests and license](0001-toolchain.md) | Accepted for milestone 1 |
| [0002 Rational time](0002-rational-time.md) | Accepted with limits |
| [0003 Native model/persistence](0003-native-model.md) | Accepted for milestone 1 |
| [0004 Commands/history](0004-commands.md) | Accepted for milestone 1; extended by 0007 |
| [0005 Media/desktop strategy](0005-media-backend.md) | Probing selected in 0009; preview/UI selected in 0010 |
| [0006 MCP boundary](0006-mcp-boundary.md) | Boundary accepted; SDK deferred |

Deferred choices require prototypes or measured evidence.

Milestone 2 additions (2026-09-10):

| ADR | Status |
| --- | --- |
| [0007 Editing sessions](0007-editing-sessions.md) | Accepted; extends 0004 |
| [0008 Identity and migration](0008-identity-and-migration.md) | Accepted; extends 0003 |

Earlier ADRs document milestone 1 decisions. Transaction deferrals in 0004 and revision/
attribution prerequisites in 0006 are now implemented as described in 0007.
Milestone 3: [0009 Optional ffprobe discovery adapter](0009-media-probing.md) — accepted.

Milestone 4: [0010 Qt desktop and isolated preview worker](0010-desktop-playback.md) — accepted.

Milestone 5: [0011 Shared source origins and decoded frame lookup](0011-source-origins-and-frame-index.md) — accepted.

- [0012: Local MCP editing adapter](0012-local-mcp-adapter.md)

- [0013: Shared document ownership and recovery checkpoints](0013-document-ownership-and-recovery.md)

- [0014: Multi-track decoding backend](0014-multitrack-backend.md)

- [0015: Fixed-revision local export](0015-local-export.md) — Windows synthetic acceptance passed; platform and failure gaps remain.
