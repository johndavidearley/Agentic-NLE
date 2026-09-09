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
    bool operator==(const ProjectSnapshot &) const = default;
};
void validate(const ProjectSnapshot &project);
bool clip_less(const Clip &a, const Clip &b);
bool supports(MediaKind media, TrackKind track);
} // namespace nle
