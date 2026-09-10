#include "commands/editor.hpp"
#include <algorithm>
#include <limits>
#include <random>
#include <sstream>
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

CommandResult apply(ProjectSnapshot &candidate, const Command &command) {
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
                },
                [&](const DeleteTrack &c) {
                    for (auto &sequence : candidate.sequences) {
                        const auto it =
                            std::find_if(sequence.tracks.begin(), sequence.tracks.end(),
                                         [&](const auto &track) { return track.id == c.track; });
                        if (it != sequence.tracks.end()) {
                            sequence.tracks.erase(it);
                            return;
                        }
                    }
                    throw DomainError("track not found");
                },
                [&](const ReorderTrack &c) {
                    auto &sequence = find_sequence(candidate, c.sequence);
                    if (c.index >= sequence.tracks.size())
                        throw DomainError("track index out of range");
                    const auto it =
                        std::find_if(sequence.tracks.begin(), sequence.tracks.end(),
                                     [&](const auto &track) { return track.id == c.track; });
                    if (it == sequence.tracks.end())
                        throw DomainError("track not found in sequence");
                    auto track = std::move(*it);
                    sequence.tracks.erase(it);
                    sequence.tracks.insert(sequence.tracks.begin() +
                                               static_cast<std::ptrdiff_t>(c.index),
                                           std::move(track));
                },
                [&](const RelinkMedia &c) {
                    if (c.role != LocationRole::Original && c.role != LocationRole::Proxy)
                        throw DomainError("invalid location role");
                    for (auto &media : candidate.media) {
                        if (media.id != c.media)
                            continue;
                        auto it = std::find_if(
                            media.locations.begin(), media.locations.end(),
                            [&](const auto &location) { return location.role == c.role; });
                        if (!c.uri) {
                            if (it != media.locations.end())
                                media.locations.erase(it);
                        } else if (it == media.locations.end()) {
                            media.locations.push_back({c.role, *c.uri});
                        } else {
                            it->uri = *c.uri;
                        }
                        return;
                    }
                    throw DomainError("media not found");
                }},
        command);
    sort_tracks(candidate);
    validate(candidate);
    return result;
}
std::string describe(const Command &command, const CommandResult &result) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    const auto time = [&](RationalTime t) { out << t.value() << '/' << t.rate(); };
    std::visit(Visitor{[&](const CreateSequence &) {
                           out << "CreateSequence sequence=" << result.sequence->value;
                       },
                       [&](const CreateTrack &c) {
                           out << "CreateTrack sequence=" << c.sequence.value
                               << " track=" << result.track->value;
                       },
                       [&](const RegisterMedia &) {
                           out << "RegisterMedia media=" << result.media->value;
                       },
                       [&](const InsertClip &c) {
                           out << "InsertClip clip=" << result.clip->value
                               << " media=" << c.media.value << " track=" << c.track.value
                               << " position=";
                           time(c.position);
                       },
                       [&](const MoveClip &c) {
                           out << "MoveClip clip=" << c.clip.value << " track=" << c.track.value
                               << " position=";
                           time(c.position);
                       },
                       [&](const TrimClip &c) {
                           out << "TrimClip clip=" << c.clip.value << " position=";
                           time(c.position);
                           out << " source=";
                           time(c.source.start);
                           out << " duration=";
                           time(c.source.duration);
                       },
                       [&](const SplitClip &c) {
                           out << "SplitClip clip=" << c.clip.value
                               << " right=" << result.clip->value << " position=";
                           time(c.position);
                       },
                       [&](const DeleteClip &c) { out << "DeleteClip clip=" << c.clip.value; },
                       [&](const DeleteTrack &c) { out << "DeleteTrack track=" << c.track.value; },
                       [&](const ReorderTrack &c) {
                           out << "ReorderTrack sequence=" << c.sequence.value
                               << " track=" << c.track.value << " index=" << c.index;
                       },
                       [&](const RelinkMedia &c) {
                           out << "RelinkMedia media=" << c.media.value
                               << " role=" << static_cast<int>(c.role)
                               << (c.uri ? " set" : " remove");
                       }},
               command);
    return out.str();
}
ProjectSnapshot content_only(const ProjectSnapshot &source) {
    auto result = source;
    result.operations = {};
    result.operations.shrink_to_fit();
    result.revision = 0;
    return result;
}
void append_record(ProjectSnapshot &candidate, const RequestContext &context, ChangeKind kind,
                   OperationId target, std::vector<std::string> actions) {
    if (candidate.operations.size() >= max_operations)
        throw DomainError("operation log is full; no edit was committed");
    ++candidate.revision; // Validation limits revisions to max_operations.
    candidate.operations.push_back({OperationId{candidate.revision}, candidate.revision,
                                    context.actor, context.label, kind, target,
                                    std::move(actions)});
    validate(candidate);
}
} // namespace

Transaction::Transaction(ProjectSnapshot state, RequestContext context,
                         std::weak_ptr<const int> owner)
    : candidate_(std::move(state)), context_(std::move(context)), owner_(std::move(owner)),
      base_revision_(candidate_->revision) {}
ProjectSnapshot Transaction::preview() const {
    if (!active())
        throw DomainError("transaction is closed");
    return *candidate_;
}
CommandResult Transaction::execute(const Command &command) {
    if (!active())
        throw DomainError("transaction is closed");
    try {
        if (commands_ >= max_batch_commands)
            throw DomainError("transaction command limit exceeded");
        auto candidate = *candidate_;
        auto result = apply(candidate, command);
        if (candidate != *candidate_)
            actions_.push_back(describe(command, result));
        candidate_ = std::move(candidate);
        ++commands_;
        result.revision = base_revision_;
        return result;
    } catch (...) {
        rollback(); // Failed batches can never accidentally commit their successful prefix.
        throw;
    }
}

Editor::Editor(std::string name, HistoryLimits limits)
    : Editor(ProjectSnapshot{make_project_id(), std::move(name), 1, {}, {}}, limits) {}
Editor::Editor(ProjectSnapshot project, HistoryLimits limits)
    : state_(std::move(project)), limits_(limits) {
    validate(state_);
}
ProjectSnapshot Editor::snapshot() const {
    const std::lock_guard lock(mutex_);
    return state_;
}
std::uint64_t Editor::revision() const {
    const std::lock_guard lock(mutex_);
    return state_.revision;
}
bool Editor::can_undo() const {
    const std::lock_guard lock(mutex_);
    return !undo_.empty();
}
bool Editor::can_redo() const {
    const std::lock_guard lock(mutex_);
    return !redo_.empty();
}
HistoryUsage Editor::history_usage() const {
    const std::lock_guard lock(mutex_);
    std::size_t bytes = 0;
    for (const auto &edit : undo_)
        bytes += edit->bytes;
    for (const auto &edit : redo_)
        bytes += edit->bytes;
    return {undo_.size(), redo_.size(), bytes};
}
void Editor::check_context(const RequestContext &context) const {
    validate_actor(context.actor);
    validate_text(context.label);
    if (context.expected_revision && *context.expected_revision != state_.revision)
        throw RevisionConflict("project revision changed");
}
Transaction Editor::begin(RequestContext context) const {
    const std::lock_guard lock(mutex_);
    check_context(context);
    return Transaction(state_, std::move(context), identity_);
}
CommandResult Editor::execute(const Command &command, RequestContext context) {
    const std::lock_guard lock(mutex_);
    check_context(context);
    auto candidate = state_;
    auto result = apply(candidate, command);
    if (candidate != state_)
        result.operation = publish(std::move(candidate), context, {describe(command, result)});
    result.revision = state_.revision;
    return result;
}
std::optional<OperationId> Editor::commit(Transaction &&transaction) {
    const std::lock_guard lock(mutex_);
    try {
        if (!transaction.active() || transaction.owner_.lock() != identity_)
            throw DomainError("transaction does not belong to this live session");
        if (transaction.base_revision_ != state_.revision)
            throw RevisionConflict("transaction base revision is stale");
        check_context(transaction.context_);
        auto result = publish(std::move(*transaction.candidate_), transaction.context_,
                              std::move(transaction.actions_));
        transaction.rollback();
        return result;
    } catch (...) {
        transaction.rollback();
        throw;
    }
}
std::optional<OperationId> Editor::publish(ProjectSnapshot candidate, const RequestContext &context,
                                           std::vector<std::string> actions) {
    if (candidate == state_)
        return std::nullopt;
    auto before = content_only(state_);
    auto after = content_only(candidate);
    append_record(candidate, context, ChangeKind::Edit, {}, std::move(actions));
    const auto operation = candidate.operations.back().id;
    auto history = undo_; // Shared immutable entries make this cheap and exception-safe.
    if (limits_.entries == 0 || limits_.bytes == 0) {
        history.clear();
    } else {
        const auto bytes = sizeof(Edit) + snapshot_bytes(before) + snapshot_bytes(after);
        if (bytes > limits_.bytes)
            throw DomainError("edit exceeds undo memory budget");
        history.push_back(std::make_shared<const Edit>(
            Edit{std::move(before), std::move(after), operation, bytes}));
        std::size_t total = 0;
        for (const auto &edit : history)
            total += edit->bytes;
        while (history.size() > limits_.entries || total > limits_.bytes) {
            total -= history.front()->bytes;
            history.erase(history.begin());
        }
    }
    // All potentially throwing work is complete before publishing anything.
    state_ = std::move(candidate);
    undo_ = std::move(history);
    redo_.clear();
    return operation;
}
bool Editor::restore(bool redo, const RequestContext &context) {
    check_context(context);
    auto &from = redo ? redo_ : undo_;
    auto &to = redo ? undo_ : redo_;
    if (from.empty())
        return false;
    const auto edit = from.back();
    auto candidate = redo ? edit->after : edit->before;
    candidate.next_id = std::max(candidate.next_id, state_.next_id);
    candidate.revision = state_.revision;
    candidate.operations = state_.operations;
    append_record(candidate, context, redo ? ChangeKind::Redo : ChangeKind::Undo, edit->operation,
                  {});
    to.push_back(edit); // Allocate before the state transition.
    state_ = std::move(candidate);
    from.pop_back();
    return true;
}
bool Editor::undo(RequestContext context) {
    const std::lock_guard lock(mutex_);
    return restore(false, context);
}
bool Editor::redo(RequestContext context) {
    const std::lock_guard lock(mutex_);
    return restore(true, context);
}
std::vector<OperationRecord> Editor::changes_since(std::uint64_t revision) const {
    const std::lock_guard lock(mutex_);
    if (revision > state_.revision)
        throw RevisionConflict("revision is ahead of project");
    return {state_.operations.begin() + static_cast<std::ptrdiff_t>(revision),
            state_.operations.end()};
}
} // namespace nle