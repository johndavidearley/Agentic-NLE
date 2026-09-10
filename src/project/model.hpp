#pragma once
#include "core/time.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace nle {
template <typename Tag> struct Id {
    std::uint64_t value = 0;
    auto operator<=>(const Id &) const = default;
};
using ProjectId = Id<struct ProjectTag>;
using SequenceId = Id<struct SequenceTag>;
using TrackId = Id<struct TrackTag>;
using ClipId = Id<struct ClipTag>;
using MediaId = Id<struct MediaTag>;

using OperationId = Id<struct OperationTag>;
struct ActorId {
    std::string value = "local";
    bool operator==(const ActorId &) const = default;
};
enum class ActorKind { Human, Agent, System };
struct Actor {
    ActorId id;
    ActorKind kind = ActorKind::Human;
    bool operator==(const Actor &) const = default;
};
enum class ChangeKind { Edit, Undo, Redo };
struct OperationRecord {
    OperationId id;
    std::uint64_t revision = 0;
    Actor actor;
    std::string label;
    ChangeKind kind = ChangeKind::Edit;
    OperationId target;               // Undo/redo refer to the original committed edit.
    std::vector<std::string> actions; // Descriptive operation summaries, not replay instructions.
    bool operator==(const OperationRecord &) const = default;
};
inline constexpr std::size_t max_operations = 10000;
inline constexpr std::size_t max_batch_commands = 1024;
void validate_actor(const Actor &actor);
void validate_text(const std::string &text);
enum class TrackKind { Video, Audio };
enum class MediaKind { Video, Audio, AudioVideo };
enum class LocationRole { Original, Proxy };
struct MediaLocation {
    LocationRole role;
    std::string uri;
    bool operator==(const MediaLocation &) const = default;
};
struct MediaAsset {
    MediaId id;
    std::string name;
    MediaKind kind;
    RationalTime duration;
    // Empty locations represent an offline logical asset. Generated media is deferred.
    std::vector<MediaLocation> locations;
    bool operator==(const MediaAsset &) const = default;
};
struct Clip {
    ClipId id;
    MediaId media;
    RationalTime position;
    TimeRange source;
    bool operator==(const Clip &) const = default;
};
struct Track {
    TrackId id;
    std::string name;
    TrackKind kind;
    std::vector<Clip> clips;
    bool operator==(const Track &) const = default;
};
struct Sequence {
    SequenceId id;
    std::string name;
    RationalTime frame_duration;
    std::vector<Track> tracks;
    bool operator==(const Sequence &) const = default;
};
// Detached value DTO. Modifying a snapshot cannot mutate an Editor.
struct ProjectSnapshot {
    ProjectId id;
    std::string name;
    std::uint64_t next_id = 1;
    std::vector<MediaAsset> media;
    std::vector<Sequence> sequences;
    std::uint64_t revision = 0;
    std::vector<OperationRecord> operations{};
    bool operator==(const ProjectSnapshot &) const = default;
};
void validate(const ProjectSnapshot &project);
std::size_t snapshot_bytes(const ProjectSnapshot &project);
bool clip_less(const Clip &a, const Clip &b);
bool supports(MediaKind media, TrackKind track);
} // namespace nle
