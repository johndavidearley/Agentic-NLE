#include "commands/editor.hpp"
#include <algorithm>
#include <limits>
#include <random>
#include <utility>

namespace nle {
namespace {
template <typename... Ts> struct Visitor : Ts... {
    using Ts::operator()...;
};
template <typename T> T allocate(ProjectSnapshot &state) {
    if (state.next_id == std::numeric_limits<std::uint64_t>::max())
        throw DomainError("object ID space exhausted");
    return T{state.next_id++};
}
Sequence &find_sequence(ProjectSnapshot &state, SequenceId id) {
    for (auto &sequence : state.sequences)
        if (sequence.id == id)
            return sequence;
    throw DomainError("sequence not found");
}
Track &find_track(ProjectSnapshot &state, TrackId id) {
    for (auto &sequence : state.sequences)
        for (auto &track : sequence.tracks)
            if (track.id == id)
                return track;
    throw DomainError("track not found");
}
std::pair<Track *, std::size_t> find_clip(ProjectSnapshot &state, ClipId id) {
    for (auto &sequence : state.sequences)
        for (auto &track : sequence.tracks)
            for (std::size_t i = 0; i < track.clips.size(); ++i)
                if (track.clips[i].id == id)
                    return {&track, i};
    throw DomainError("clip not found");
}
void sort_tracks(ProjectSnapshot &state) {
    for (auto &sequence : state.sequences)
        for (auto &track : sequence.tracks)
            std::sort(track.clips.begin(), track.clips.end(), clip_less);
}
ProjectId make_project_id() {
    std::random_device random;
    std::uniform_int_distribution<std::uint64_t> distribution(
        1, std::numeric_limits<std::uint64_t>::max());
    return ProjectId{distribution(random)};
}
} // namespace

Editor::Editor(std::string name)
    : Editor(ProjectSnapshot{make_project_id(), std::move(name), 1, {}, {}}) {}
Editor::Editor(ProjectSnapshot project) : state_(std::move(project)) { validate(state_); }

CommandResult Editor::execute(const Command &command) {
    auto candidate = state_;
    CommandResult result;
    std::visit(
        Visitor{[&](const CreateSequence &c) {
                    const auto id = allocate<SequenceId>(candidate);
                    candidate.sequences.push_back({id, c.name, c.frame_duration, {}});
                    result.sequence = id;
                },
                [&](const CreateTrack &c) {
                    auto &sequence = find_sequence(candidate, c.sequence);
                    const auto id = allocate<TrackId>(candidate);
                    sequence.tracks.push_back({id, c.name, c.kind, {}});
                    result.track = id;
                },
                [&](const RegisterMedia &c) {
                    const auto id = allocate<MediaId>(candidate);
                    candidate.media.push_back({id, c.name, c.kind, c.duration, c.locations});
                    result.media = id;
                },
                [&](const InsertClip &c) {
                    auto &track = find_track(candidate, c.track);
                    const auto id = allocate<ClipId>(candidate);
                    track.clips.push_back({id, c.media, c.position, c.source});
                    result.clip = id;
                },
                [&](const MoveClip &c) {
                    auto [source, index] = find_clip(candidate, c.clip);
                    auto &destination = find_track(candidate, c.track);
                    auto clip = source->clips[index];
                    clip.position = c.position;
                    source->clips.erase(source->clips.begin() + static_cast<std::ptrdiff_t>(index));
                    destination.clips.push_back(clip);
                },
                [&](const TrimClip &c) {
                    auto [track, index] = find_clip(candidate, c.clip);
                    track->clips[index].position = c.position;
                    track->clips[index].source = c.source;
                },
                [&](const SplitClip &c) {
                    auto [track, index] = find_clip(candidate, c.clip);
                    auto &left = track->clips[index];
                    const auto end = left.position + left.source.duration;
                    if (c.position <= left.position || c.position >= end)
                        throw DomainError("split must be strictly inside clip");
                    const auto offset = c.position - left.position;
                    const auto right_id = allocate<ClipId>(candidate);
                    Clip right{right_id,
                               left.media,
                               c.position,
                               {left.source.start + offset, left.source.duration - offset}};
                    left.source.duration = offset;
                    track->clips.push_back(right);
                    result.clip = right_id;
                },
                [&](const DeleteClip &c) {
                    auto [track, index] = find_clip(candidate, c.clip);
                    track->clips.erase(track->clips.begin() + static_cast<std::ptrdiff_t>(index));
                }},
        command);
    sort_tracks(candidate);
    validate(candidate);
    if (candidate == state_)
        return result; // A no-op preserves redo history.
    // Allocate history before changing live state, preserving the strong exception guarantee.
    undo_.push_back(Edit{state_, candidate});
    state_ = std::move(candidate);
    redo_.clear();
    return result;
}

bool Editor::undo() {
    if (undo_.empty())
        return false;
    auto restored = undo_.back().before;
    restored.next_id = std::max(restored.next_id, state_.next_id);
    redo_.push_back(undo_.back());
    state_ = std::move(restored);
    undo_.pop_back();
    return true;
}
bool Editor::redo() {
    if (redo_.empty())
        return false;
    auto restored = redo_.back().after;
    restored.next_id = std::max(restored.next_id, state_.next_id);
    undo_.push_back(redo_.back());
    state_ = std::move(restored);
    redo_.pop_back();
    return true;
}
} // namespace nle
