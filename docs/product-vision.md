# Product vision

Build a professional open-source non-linear video editor for humans and software
agents. Human and automated edits produce the same editable, inspectable, undoable timeline.

Long-term influences are Resolve's workflow separation and media/color/delivery depth,
Premiere's timeline editing, and CapCut's fast creator workflows. These are product
references, not borrowed architectures or implementations.

Agents are domain clients using stable IDs and explicit operations. They do not press
buttons or own a separate edit representation. MCP is one adapter alongside UI and CLI.

Milestones 1–6 provide the headless model, hardened editing sessions, media probing,
a Qt playback preview, source timing and frame precision, and a local MCP editing surface.
Automated results remain editable and attributable to concrete operations. Revisions and
actor/operation metadata persist in the native project. Interchange, complete replayable
provenance and reproducible media execution remain future work.

Color, compositing, multicam, AI generation, advanced audio, cloud collaboration,
and a plugin marketplace are outside the bootstrap.
