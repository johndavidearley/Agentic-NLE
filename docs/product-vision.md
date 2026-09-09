# Product vision

Build a professional open-source non-linear video editor for humans and software
agents. Human and automated edits produce the same editable, inspectable, undoable timeline.

Long-term influences are Resolve's workflow separation and media/color/delivery depth,
Premiere's timeline editing, and CapCut's fast creator workflows. These are product
references, not borrowed architectures or implementations.

Agents are domain clients using stable IDs and explicit operations. They do not press
buttons or own a separate edit representation. MCP is one adapter alongside UI and CLI.

Milestone 1 proves the headless model. Later work can add probing, playback, Qt,
interchange, and a small MCP surface. Automated results should remain editable and
attributable to concrete operations. Durable provenance and reproducible media execution
are future work; this bootstrap provides deterministic editing and persistent state.

Color, compositing, multicam, AI generation, advanced audio, cloud collaboration,
and a plugin marketplace are outside the bootstrap.
