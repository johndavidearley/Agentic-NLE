#pragma once
#include "commands/editor.hpp"
#include <span>
namespace nle {
enum class ClipGesture { Move, TrimLeft, TrimRight };
// Exact targets win over pixel-derived time; ties prefer playhead/clip edges over frames.
RationalTime snap_time(RationalTime position, RationalTime frame, RationalTime playhead,
                       std::span<const RationalTime> edges, RationalTime tolerance);
Command timeline_drag(const ProjectSnapshot &project, SequenceId sequence, ClipId clip,
                      TrackId destination, ClipGesture gesture, RationalTime target, bool snapping,
                      RationalTime playhead, RationalTime tolerance);
} // namespace nle
