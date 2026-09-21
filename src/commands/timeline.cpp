#include "commands/timeline.hpp"
#include <algorithm>
#include <limits>
namespace nle {
namespace {
RationalTime distance(RationalTime a, RationalTime b) { return a >= b ? a - b : b - a; }
std::optional<RationalTime> nearest_snap(RationalTime position, RationalTime frame,
                                         RationalTime playhead, std::span<const RationalTime> edges,
                                         RationalTime tolerance) {
    if (frame == RationalTime{} || frame.value() > 1000000000 || frame.rate() > 1000000000 ||
        position > RationalTime{86400})
        throw DomainError("Timeline snapping supports up to 24 hours and bounded frame rates");
    auto result = position;
    auto best = tolerance;
    bool found = false;
    const auto consider = [&](RationalTime candidate) {
        const auto delta = distance(position, candidate);
        if (delta <= tolerance && (!found || delta < best)) {
            result = candidate;
            best = delta;
            found = true;
        }
    };
    consider(playhead);
    for (const auto edge : edges)
        consider(edge);
    const long double estimate = static_cast<long double>(position.value()) * frame.rate() /
                                 (static_cast<long double>(position.rate()) * frame.value());
    if (estimate >
        static_cast<long double>(std::numeric_limits<std::int64_t>::max() / frame.value() - 2))
        throw DomainError("Timeline frame index exceeds supported range");
    auto index = static_cast<std::int64_t>(estimate);
    const auto at = [&](std::int64_t i) { return RationalTime{i * frame.value(), frame.rate()}; };
    while (index > 0 && at(index) > position)
        --index;
    while (at(index + 1) <= position)
        ++index;
    consider(at(index));
    consider(at(index + 1));
    return found ? std::optional<RationalTime>{result} : std::nullopt;
}
} // namespace
RationalTime snap_time(RationalTime position, RationalTime frame, RationalTime playhead,
                       std::span<const RationalTime> edges, RationalTime tolerance) {
    return nearest_snap(position, frame, playhead, edges, tolerance).value_or(position);
}
Command timeline_drag(const ProjectSnapshot &project, SequenceId sequence, ClipId id,
                      TrackId destination, ClipGesture gesture, RationalTime target, bool snapping,
                      RationalTime playhead, RationalTime tolerance) {
    const auto seq = std::find_if(project.sequences.begin(), project.sequences.end(),
                                  [&](const auto &s) { return s.id == sequence; });
    if (seq == project.sequences.end())
        throw DomainError("Sequence no longer exists");
    const Clip *clip = nullptr;
    TrackId original;
    bool track_found = false;
    std::vector<RationalTime> edges;
    for (const auto &track : seq->tracks) {
        track_found = track_found || track.id == destination;
        for (const auto &item : track.clips) {
            if (item.id == id) {
                clip = &item;
                original = track.id;
            } else {
                edges.push_back(item.position);
                edges.push_back(item.position + item.source.duration);
            }
        }
    }
    if (!clip || !track_found)
        throw DomainError("Drop on an existing track");
    if (gesture != ClipGesture::Move && destination != original)
        throw DomainError("Trim on the clip's original track");
    if (snapping) {
        const auto left = nearest_snap(target, seq->frame_duration, playhead, edges, tolerance);
        // A moving clip can snap either its start or its end.
        if (gesture == ClipGesture::Move) {
            const auto end = target + clip->source.duration;
            const auto right = nearest_snap(end, seq->frame_duration, playhead, edges, tolerance);
            if (right && *right >= clip->source.duration &&
                (!left || distance(end, *right) < distance(target, *left)))
                target = *right - clip->source.duration;
            else
                target = left.value_or(target);
        } else
            target = left.value_or(target);
    }
    if (gesture == ClipGesture::Move)
        return MoveClip{id, destination, target};
    const auto end = clip->position + clip->source.duration;
    if (gesture == ClipGesture::TrimRight) {
        if (target <= clip->position)
            throw DomainError("A clip must keep a positive duration");
        return TrimClip{id, clip->position, {clip->source.start, target - clip->position}};
    }
    if (target >= end)
        throw DomainError("A clip must keep a positive duration");
    auto source = clip->source.start;
    if (target >= clip->position)
        source = source + (target - clip->position);
    else {
        const auto extension = clip->position - target;
        if (extension > source)
            throw DomainError("The source has no earlier media to reveal");
        source = source - extension;
    }
    return TrimClip{id, target, {source, end - target}};
}
} // namespace nle
