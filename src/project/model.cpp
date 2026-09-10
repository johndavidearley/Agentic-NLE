#include "project/model.hpp"
#include <algorithm>
#include <map>
#include <set>

namespace nle {
void validate_text(const std::string &text) {
    if (text.empty() || text.size() > 4096)
        throw DomainError("text must contain 1 to 4096 bytes");
    for (char byte : text)
        if (const auto c = static_cast<unsigned char>(byte); c < 32 || c == 127)
            throw DomainError("control characters are not allowed in text");
}

void validate_actor(const Actor &actor) {
    validate_text(actor.id.value);
    if (actor.kind != ActorKind::Human && actor.kind != ActorKind::Agent &&
        actor.kind != ActorKind::System)
        throw DomainError("invalid actor kind");
}
bool clip_less(const Clip &a, const Clip &b) {
    return a.position == b.position ? a.id < b.id : a.position < b.position;
}
bool supports(MediaKind media, TrackKind track) {
    return media == MediaKind::AudioVideo ||
           (media == MediaKind::Video && track == TrackKind::Video) ||
           (media == MediaKind::Audio && track == TrackKind::Audio);
}
void validate(const ProjectSnapshot &project) {
    if (project.id.value == 0 || project.next_id == 0)
        throw DomainError("invalid project identity or allocation watermark");
    validate_text(project.name);
    if (project.operations.size() > max_operations || project.revision != project.operations.size())
        throw DomainError("invalid project revision or operation count");
    std::uint64_t expected = 0;
    for (const auto &operation : project.operations) {
        ++expected;
        if (operation.id.value != expected || operation.revision != expected)
            throw DomainError("operation identities/revisions must be contiguous");
        validate_actor(operation.actor);
        validate_text(operation.label);
        if (operation.kind == ChangeKind::Edit) {
            if (operation.target.value != 0 || operation.actions.empty() ||
                operation.actions.size() > max_batch_commands)
                throw DomainError("invalid edit attribution");
        } else if (operation.kind == ChangeKind::Undo || operation.kind == ChangeKind::Redo) {
            if (!operation.actions.empty() || operation.target.value == 0 ||
                operation.target.value >= expected ||
                project.operations[static_cast<std::size_t>(operation.target.value - 1)].kind !=
                    ChangeKind::Edit)
                throw DomainError("invalid undo/redo attribution target");
        } else {
            throw DomainError("unknown operation kind");
        }
        for (const auto &action : operation.actions)
            validate_text(action);
    }
    std::set<std::uint64_t> ids;
    std::map<MediaId, const MediaAsset *> media_index;
    const auto check_id = [&](std::uint64_t id) {
        if (id == 0 || id >= project.next_id || !ids.insert(id).second)
            throw DomainError("duplicate, zero, or unallocated object ID");
    };
    for (const auto &asset : project.media) {
        check_id(asset.id.value);
        media_index.emplace(asset.id, &asset);
        validate_text(asset.name);
        if (asset.kind != MediaKind::Video && asset.kind != MediaKind::Audio &&
            asset.kind != MediaKind::AudioVideo)
            throw DomainError("invalid media kind");
        if (asset.duration == RationalTime{})
            throw DomainError("media duration must be positive");
        std::set<LocationRole> roles;
        for (const auto &location : asset.locations) {
            if (location.role != LocationRole::Original && location.role != LocationRole::Proxy)
                throw DomainError("invalid location role");
            if (!roles.insert(location.role).second)
                throw DomainError("duplicate location role");
            validate_text(location.uri);
        }
    }
    for (const auto &sequence : project.sequences) {
        check_id(sequence.id.value);
        validate_text(sequence.name);
        if (sequence.frame_duration == RationalTime{})
            throw DomainError("frame duration must be positive");
        for (const auto &track : sequence.tracks) {
            check_id(track.id.value);
            validate_text(track.name);
            if (track.kind != TrackKind::Video && track.kind != TrackKind::Audio)
                throw DomainError("invalid track kind");
            if (!std::is_sorted(track.clips.begin(), track.clips.end(), clip_less))
                throw DomainError("clips must be in timeline order");
            RationalTime previous_end;
            for (const auto &clip : track.clips) {
                check_id(clip.id.value);
                const auto asset = media_index.find(clip.media);
                if (asset == media_index.end())
                    throw DomainError("clip references missing media");
                if (!supports(asset->second->kind, track.kind))
                    throw DomainError("media does not support track kind");
                if (clip.source.duration == RationalTime{} ||
                    clip.source.end() > asset->second->duration)
                    throw DomainError("invalid clip source range");
                if (clip.position < previous_end)
                    throw DomainError("overlapping clips are not supported in milestone 1");
                previous_end = clip.position + clip.source.duration;
            }
        }
    }
}
std::size_t snapshot_bytes(const ProjectSnapshot &project) {
    // Conservative accounting includes reserved vector storage and all string capacities.
    // String SSO may be counted twice. Allocator overhead and shared_ptr nodes are excluded.
    std::size_t bytes = sizeof(ProjectSnapshot) + project.name.capacity() + 1;
    bytes += project.media.capacity() * sizeof(MediaAsset);
    for (const auto &media : project.media) {
        bytes += media.name.capacity() + 1 + media.locations.capacity() * sizeof(MediaLocation);
        for (const auto &location : media.locations)
            bytes += location.uri.capacity() + 1;
    }
    bytes += project.sequences.capacity() * sizeof(Sequence);
    for (const auto &sequence : project.sequences) {
        bytes += sequence.name.capacity() + 1 + sequence.tracks.capacity() * sizeof(Track);
        for (const auto &track : sequence.tracks)
            bytes += track.name.capacity() + 1 + track.clips.capacity() * sizeof(Clip);
    }
    bytes += project.operations.capacity() * sizeof(OperationRecord);
    for (const auto &operation : project.operations) {
        bytes += operation.actor.id.value.capacity() + operation.label.capacity() + 2;
        bytes += operation.actions.capacity() * sizeof(std::string);
        for (const auto &action : operation.actions)
            bytes += action.capacity() + 1;
    }
    return bytes;
}
} // namespace nle
