#include "mcp/wire.hpp"
#include <algorithm>
#include <charconv>
#include <limits>
#include <set>
namespace nle::mcp {
void fields(const Json &value, std::initializer_list<std::string_view> required,
            std::initializer_list<std::string_view> optional) {
    if (!value.is_object())
        throw Failure("invalid_arguments", "Expected an object.");
    for (auto key : required)
        if (!value.contains(key))
            throw Failure("invalid_arguments", "Missing field: " + std::string(key));
    for (const auto &[key, ignored] : value.items()) {
        (void)ignored;
        if (std::find(required.begin(), required.end(), key) == required.end() &&
            std::find(optional.begin(), optional.end(), key) == optional.end())
            throw Failure("invalid_arguments", "Unexpected field.");
    }
}
std::string text(const Json &value, std::size_t limit) {
    if (!value.is_string())
        throw Failure("invalid_arguments", "Expected a string.");
    auto result = value.get<std::string>();
    if (result.empty() || result.size() > limit)
        throw Failure("invalid_arguments", "String length is outside the supported range.");
    validate_text(result);
    return result;
}
std::uint64_t number(const Json &value, bool zero) {
    const auto str = text(value, 20);
    std::uint64_t result = 0;
    const auto [end, error] = std::from_chars(str.data(), str.data() + str.size(), result);
    if (error != std::errc{} || end != str.data() + str.size() ||
        (str.size() > 1 && str.front() == '0') || (!zero && result == 0))
        throw Failure("invalid_arguments", "Expected a canonical unsigned decimal string.");
    return result;
}
RationalTime time(const Json &value) {
    fields(value, {"value", "rate"});
    const auto numerator = number(value.at("value")), denominator = number(value.at("rate"), false);
    const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (numerator > maximum || denominator > maximum)
        throw Failure("invalid_arguments", "Time is outside the signed 64-bit range.");
    return {static_cast<std::int64_t>(numerator), static_cast<std::int64_t>(denominator)};
}
Json time(RationalTime value) {
    return {{"value", std::to_string(value.value())}, {"rate", std::to_string(value.rate())}};
}
Json operation(const OperationRecord &value) {
    return {{"id", std::to_string(value.id.value)},
            {"revision", std::to_string(value.revision)},
            {"kind", value.kind == ChangeKind::Edit   ? "edit"
                     : value.kind == ChangeKind::Undo ? "undo"
                                                      : "redo"},
            {"target", std::to_string(value.target.value)},
            {"actor",
             {{"id", value.actor.id.value},
              {"kind", value.actor.kind == ActorKind::Agent   ? "agent"
                       : value.actor.kind == ActorKind::Human ? "human"
                                                              : "system"}}},
            {"label", value.label},
            {"actions", value.actions}};
}
Json snapshot(const ProjectSnapshot &project) {
    Json assets = Json::array(), sequences = Json::array();
    for (const auto &asset : project.media) {
        Json locations = Json::array();
        for (const auto &location : asset.locations)
            locations.push_back(
                {{"role", location.role == LocationRole::Original ? "original" : "proxy"},
                 {"uri", location.uri}});
        Json source = nullptr;
        if (asset.source) {
            const auto &metadata = *asset.source;
            Json streams = Json::array();
            for (const auto &stream : metadata.streams) {
                Json entry{{"index", stream.index},
                           {"kind", stream.kind == TrackKind::Video ? "video" : "audio"},
                           {"codec", stream.codec},
                           {"time_base", time(stream.time_base)},
                           {"start_ticks", stream.start_known
                                               ? Json(std::to_string(stream.start_ticks))
                                               : Json(nullptr)},
                           {"duration_ticks", stream.duration_ticks < 0
                                                  ? Json(nullptr)
                                                  : Json(std::to_string(stream.duration_ticks))},
                           {"duration_estimate", time(stream.duration_estimate)},
                           {"frame_duration", time(stream.frame_duration)},
                           {"nominal_frame_duration", time(stream.nominal_frame_duration)},
                           {"width", stream.width},
                           {"height", stream.height},
                           {"sample_rate", stream.sample_rate},
                           {"channels", stream.channels}};
                if (metadata.time_mode == SourceTimeMode::SharedOrigin)
                    entry["source_offset"] = time(stream_offset(metadata, stream));
                streams.push_back(std::move(entry));
            }
            source = {{"container", metadata.container},
                      {"clock", metadata.time_mode == SourceTimeMode::SharedOrigin
                                    ? "shared_origin"
                                    : "legacy_per_stream"},
                      {"byte_size", std::to_string(metadata.byte_size)},
                      {"container_duration", time(metadata.container_duration)},
                      {"probe_version", metadata.probe_version},
                      {"streams", streams}};
            source["container_start"] = nullptr;
            if (metadata.container_start) {
                const auto start = *metadata.container_start;
                source["container_start"] = {
                    {"value", std::to_string(start.negative() ? -start.magnitude().value()
                                                              : start.magnitude().value())},
                    {"rate", std::to_string(start.magnitude().rate())}};
            }
        }
        assets.push_back({{"kind", "media"},
                          {"id", std::to_string(asset.id.value)},
                          {"name", asset.name},
                          {"media_kind", asset.kind == MediaKind::AudioVideo ? "audio_video"
                                         : asset.kind == MediaKind::Video    ? "video"
                                                                             : "audio"},
                          {"duration", time(asset.duration)},
                          {"locations", locations},
                          {"source", source}});
    }
    for (const auto &sequence : project.sequences) {
        Json tracks = Json::array();
        for (const auto &track : sequence.tracks) {
            Json clips = Json::array();
            for (const auto &clip : track.clips)
                clips.push_back({{"kind", "clip"},
                                 {"id", std::to_string(clip.id.value)},
                                 {"media_id", std::to_string(clip.media.value)},
                                 {"position", time(clip.position)},
                                 {"source_in", time(clip.source.start)},
                                 {"duration", time(clip.source.duration)}});
            tracks.push_back({{"kind", "track"},
                              {"id", std::to_string(track.id.value)},
                              {"track_kind", track.kind == TrackKind::Video ? "video" : "audio"},
                              {"name", track.name},
                              {"clips", clips}});
        }
        sequences.push_back({{"kind", "sequence"},
                             {"id", std::to_string(sequence.id.value)},
                             {"name", sequence.name},
                             {"frame_duration", time(sequence.frame_duration)},
                             {"tracks", tracks}});
    }
    return {{"kind", "project"},    {"id", std::to_string(project.id.value)},
            {"name", project.name}, {"revision", std::to_string(project.revision)},
            {"media", assets},      {"sequences", sequences}};
}
Json command_result(const CommandResult &value) {
    Json result = Json::object();
    if (value.sequence)
        result["sequence_id"] = std::to_string(value.sequence->value);
    if (value.track)
        result["track_id"] = std::to_string(value.track->value);
    if (value.media)
        result["media_id"] = std::to_string(value.media->value);
    if (value.clip)
        result["clip_id"] = std::to_string(value.clip->value);
    return result;
}
namespace {
template <class IdType> IdType id(const Json &value, const Aliases &aliases) {
    const auto name = text(value, 128);
    if (name.front() != '$')
        return IdType{number(value, false)};
    const auto found = aliases.find(name.substr(1));
    if (found == aliases.end())
        throw Failure("invalid_arguments", "Unknown batch alias.");
    const auto &result = found->second;
    if constexpr (std::is_same_v<IdType, SequenceId>) {
        if (result.sequence)
            return *result.sequence;
    }
    if constexpr (std::is_same_v<IdType, TrackId>) {
        if (result.track)
            return *result.track;
    }
    if constexpr (std::is_same_v<IdType, ClipId>) {
        if (result.clip)
            return *result.clip;
    }
    if constexpr (std::is_same_v<IdType, MediaId>) {
        if (result.media)
            return *result.media;
    }
    throw Failure("invalid_arguments", "Batch alias has the wrong object kind.");
}
} // namespace
Command command(const Json &value, const Aliases &aliases) {
    if (!value.is_object() || !value.contains("op"))
        throw Failure("invalid_arguments", "Command requires op.");
    const auto op = text(value.at("op"));
    if (op == "create_sequence") {
        fields(value, {"op", "name"}, {"frame_duration", "as"});
        return CreateSequence{text(value.at("name")), value.contains("frame_duration")
                                                          ? time(value.at("frame_duration"))
                                                          : RationalTime{1, 24}};
    }
    if (op == "create_track") {
        fields(value, {"op", "sequence_id", "kind", "name"}, {"as"});
        const auto kind = text(value.at("kind"));
        if (kind != "video" && kind != "audio")
            throw Failure("invalid_arguments", "Track kind must be video or audio.");
        return CreateTrack{id<SequenceId>(value.at("sequence_id"), aliases),
                           kind == "video" ? TrackKind::Video : TrackKind::Audio,
                           text(value.at("name"))};
    }
    if (op == "insert_clip") {
        fields(value, {"op", "track_id", "media_id", "position", "source_in", "duration"}, {"as"});
        return InsertClip{id<TrackId>(value.at("track_id"), aliases),
                          id<MediaId>(value.at("media_id"), aliases),
                          time(value.at("position")),
                          {time(value.at("source_in")), time(value.at("duration"))}};
    }
    if (op == "move_clip") {
        fields(value, {"op", "clip_id", "track_id", "position"});
        return MoveClip{id<ClipId>(value.at("clip_id"), aliases),
                        id<TrackId>(value.at("track_id"), aliases), time(value.at("position"))};
    }
    if (op == "trim_clip") {
        fields(value, {"op", "clip_id", "position", "source_in", "duration"});
        return TrimClip{id<ClipId>(value.at("clip_id"), aliases),
                        time(value.at("position")),
                        {time(value.at("source_in")), time(value.at("duration"))}};
    }
    if (op == "split_clip") {
        fields(value, {"op", "clip_id", "position"}, {"as"});
        return SplitClip{id<ClipId>(value.at("clip_id"), aliases), time(value.at("position"))};
    }
    if (op == "delete_clip") {
        fields(value, {"op", "clip_id"});
        return DeleteClip{id<ClipId>(value.at("clip_id"), aliases)};
    }
    if (op == "delete_track") {
        fields(value, {"op", "track_id"});
        return DeleteTrack{id<TrackId>(value.at("track_id"), aliases)};
    }
    if (op == "reorder_track") {
        fields(value, {"op", "sequence_id", "track_id", "index"});
        const auto index = number(value.at("index"));
        if (index > 100000)
            throw Failure("invalid_arguments", "Track index exceeds the record limit.");
        return ReorderTrack{id<SequenceId>(value.at("sequence_id"), aliases),
                            id<TrackId>(value.at("track_id"), aliases),
                            static_cast<std::size_t>(index)};
    }
    throw Failure("invalid_arguments", "Unsupported edit command.");
}
Json parse(std::string_view value) {
    if (value.size() > max_message_bytes)
        throw Failure("message_limit", "Message exceeds 1 MiB.");
    std::vector<std::set<std::string>> keys;
    return Json::parse(value, [&](int depth, Json::parse_event_t event, Json &part) {
        if (depth > 64)
            throw Failure("message_limit", "JSON nesting exceeds 64 levels.");
        if (event == Json::parse_event_t::object_start)
            keys.emplace_back();
        if (event == Json::parse_event_t::key &&
            !keys.back().insert(part.get<std::string>()).second)
            throw Failure("invalid_arguments", "Duplicate JSON object member.");
        if (event == Json::parse_event_t::object_end)
            keys.pop_back();
        return true;
    });
}
} // namespace nle::mcp
