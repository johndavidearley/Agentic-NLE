#include "project/model.hpp"
#include <algorithm>
#include <map>
#include <set>

namespace nle {
namespace {
void text_valid(const std::string &text) {
    if (text.empty() || text.size() > 4096)
        throw DomainError("text must contain 1 to 4096 bytes");
    for (char byte : text)
        if (const auto c = static_cast<unsigned char>(byte); c < 32 || c == 127)
            throw DomainError("control characters are not allowed in text");
}
} // namespace
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
    text_valid(project.name);
    std::set<std::uint64_t> ids;
    std::map<MediaId, const MediaAsset *> media_index;
    const auto check_id = [&](std::uint64_t id) {
        if (id == 0 || id >= project.next_id || !ids.insert(id).second)
            throw DomainError("duplicate, zero, or unallocated object ID");
    };
    for (const auto &asset : project.media) {
        check_id(asset.id.value);
        media_index.emplace(asset.id, &asset);
        text_valid(asset.name);
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
            text_valid(location.uri);
        }
    }
    for (const auto &sequence : project.sequences) {
        check_id(sequence.id.value);
        text_valid(sequence.name);
        if (sequence.frame_duration == RationalTime{})
            throw DomainError("frame duration must be positive");
        for (const auto &track : sequence.tracks) {
            check_id(track.id.value);
            text_valid(track.name);
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
} // namespace nle
