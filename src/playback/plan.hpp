#pragma once
#include "project/model.hpp"
#include <optional>
namespace nle::playback {
struct Segment {
    ClipId clip;
    RationalTime position;
    TimeRange source;
    MediaAsset asset;
    RationalTime end() const { return position + source.duration; }
};
struct Sample {
    std::optional<std::size_t> segment;
    RationalTime source;
    RationalTime next_boundary;
};
struct Plan {
    SequenceId sequence;
    std::uint64_t revision = 0;
    RationalTime duration;
    std::vector<Segment> segments;
    Sample sample(RationalTime position) const;
};
Plan make_plan(const ProjectSnapshot &project, SequenceId sequence);
// Explicit preview boundary: floor to milliseconds; project times remain exact.
std::int64_t milliseconds(RationalTime time);
void validate_preview_source(const MediaAsset &asset);
} // namespace nle::playback
