#include "project/model.hpp"
#include <algorithm>
#include <limits>
#include <map>
#include <numeric>
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
RationalTime stream_duration(const SourceStream &stream, RationalTime fallback) {
    if (stream.duration_ticks == -1)
        return stream.duration_estimate == RationalTime{} ? fallback : stream.duration_estimate;
    if (stream.duration_ticks <= 0 || stream.time_base == RationalTime{})
        throw DomainError("invalid stream duration or time base");
    const auto divisor = std::gcd(stream.duration_ticks, stream.time_base.rate());
    const auto ticks = stream.duration_ticks / divisor;
    if (ticks > std::numeric_limits<std::int64_t>::max() / stream.time_base.value())
        throw DomainError("stream duration overflow");
    return {ticks * stream.time_base.value(), stream.time_base.rate() / divisor};
}
SourceTime source_origin(const SourceMetadata &source) {
    std::optional<SourceTime> origin;
    for (const auto &stream : source.streams) {
        if (!stream.start_known && !(source.streams.size() == 1 && stream.kind == TrackKind::Audio))
            throw DomainError("shared source clock requires known stream origins");
        const auto start = SourceTime::from_ticks(stream.start_ticks, stream.time_base);
        if (!origin || start < *origin)
            origin = start;
    }
    if (!origin)
        throw DomainError("source has no streams");
    return *origin;
}
RationalTime stream_offset(const SourceMetadata &source, const SourceStream &stream) {
    return SourceTime::from_ticks(stream.start_ticks, stream.time_base)
        .since(source_origin(source));
}
RationalTime source_duration(const SourceMetadata &source) {
    std::optional<RationalTime> result;
    for (const auto &stream : source.streams) {
        auto fallback = source.container_duration;
        if (source.time_mode == SourceTimeMode::SharedOrigin &&
            source.container.find("matroska") != std::string::npos && fallback != RationalTime{})
            fallback = SourceTime{fallback.value(), fallback.rate()}.since(source_origin(source));
        auto duration = stream_duration(stream, fallback);
        if (source.time_mode == SourceTimeMode::SharedOrigin) {
            const auto offset = stream_offset(source, stream);
            // A container fallback is already a whole-source span, not a stream span.
            if (stream.duration_ticks != -1 || stream.duration_estimate != RationalTime{})
                duration = offset + duration;
        }
        if (duration == RationalTime{})
            throw DomainError("source duration unavailable");
        if (!result || (source.time_mode == SourceTimeMode::SharedOrigin ? duration > *result
                                                                         : duration < *result))
            result = duration;
    }
    if (!result)
        throw DomainError("source has no audio/video streams");
    return *result;
}
MediaKind source_kind(const SourceMetadata &source) {
    bool video = false, audio = false;
    for (const auto &stream : source.streams) {
        video |= stream.kind == TrackKind::Video;
        audio |= stream.kind == TrackKind::Audio;
    }
    if (!video && !audio)
        throw DomainError("source has no audio/video streams");
    return video && audio ? MediaKind::AudioVideo : video ? MediaKind::Video : MediaKind::Audio;
}
void validate_source(const SourceMetadata &source) {
    validate_text(source.container);
    validate_text(source.probe_version);
    validate_text(source.probe_configuration);
    if (source.byte_size == 0 || source.streams.empty() || source.streams.size() > 64)
        throw DomainError("invalid source size or stream count");
    if (source.time_mode != SourceTimeMode::LegacyPerStream &&
        source.time_mode != SourceTimeMode::SharedOrigin)
        throw DomainError("invalid source time convention");
    std::set<std::uint32_t> indices;
    for (const auto &stream : source.streams) {
        validate_text(stream.codec);
        if (!indices.insert(stream.index).second || stream.time_base == RationalTime{} ||
            stream.duration_ticks < -1 || stream.duration_ticks == 0 ||
            (!stream.start_known && stream.start_ticks != 0))
            throw DomainError("invalid stream identity or timing");
        if (stream.kind == TrackKind::Video) {
            if (!stream.width || !stream.height || stream.sample_rate || stream.channels)
                throw DomainError("invalid video stream dimensions");
        } else if (stream.kind == TrackKind::Audio) {
            if (!stream.sample_rate || !stream.channels || stream.width || stream.height ||
                stream.frame_duration != RationalTime{} ||
                stream.nominal_frame_duration != RationalTime{})
                throw DomainError("invalid audio stream properties");
        } else
            throw DomainError("invalid stream kind");
    }
    (void)source_duration(source);
}
void validate_output(const SequenceOutput &output) {
    if (output.width < 2 || output.height < 2 || output.width > 3840 || output.height > 2160 ||
        output.width % 2 != 0 || output.height % 2 != 0 || output.sample_rate != 48000 ||
        output.channels != 2)
        throw DomainError(
            "Output requires even dimensions up to 3840x2160 and 48 kHz stereo audio");
}
std::optional<std::uint32_t> select_stream(const MediaAsset &asset, TrackKind kind,
                                           const StreamSelection &selection) {
    if (selection.mode == StreamMode::Disabled || !asset.source)
        return {};
    for (const auto &stream : asset.source->streams)
        if (stream.kind == kind &&
            (selection.mode == StreamMode::Automatic || stream.index == selection.index))
            return stream.index;
    return {};
}
void validate_routing(const ClipRouting &routing, const MediaAsset &asset, TrackKind track) {
    for (const auto kind : {TrackKind::Video, TrackKind::Audio}) {
        const auto &selection = kind == TrackKind::Video ? routing.video : routing.audio;
        if (selection.mode != StreamMode::Automatic && selection.mode != StreamMode::Disabled &&
            selection.mode != StreamMode::Explicit)
            throw DomainError("Unknown stream routing mode");
        if (selection.mode != StreamMode::Explicit && selection.index != 0)
            throw DomainError("Only explicit stream routing accepts an index");
        if (selection.mode == StreamMode::Explicit &&
            (!select_stream(asset, kind, selection) ||
             (kind == TrackKind::Video && track != TrackKind::Video)))
            throw DomainError(
                "Explicit stream routing requires a matching source stream and track kind");
    }
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
        if (asset.source) {
            validate_source(*asset.source);
            if (asset.duration != source_duration(*asset.source) ||
                asset.kind != source_kind(*asset.source) ||
                std::none_of(
                    asset.locations.begin(), asset.locations.end(),
                    [](const auto &location) { return location.role == LocationRole::Original; }))
                throw DomainError("source metadata disagrees with logical asset");
        }
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
        validate_output(sequence.output);
        if (sequence.frame_duration == RationalTime{})
            throw DomainError("frame duration must be positive");
        for (const auto &track : sequence.tracks) {
            check_id(track.id.value);
            validate_text(track.name);
            if (track.playback.gain_milli > 4000)
                throw DomainError("Track gain must be between 0 and 4000 thousandths");
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
                validate_routing(clip.routing, *asset->second, track.kind);
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
        if (media.source) {
            const auto &source = *media.source;
            bytes += source.container.capacity() + source.probe_version.capacity() +
                     source.probe_configuration.capacity() + 3 +
                     source.streams.capacity() * sizeof(SourceStream);
            for (const auto &stream : source.streams)
                bytes += stream.codec.capacity() + 1;
        }
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
