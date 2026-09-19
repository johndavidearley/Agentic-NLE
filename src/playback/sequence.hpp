#pragma once
#include "project/model.hpp"
namespace nle::playback {
struct LayerClip {
    ClipId id;
    std::size_t media;
    RationalTime position;
    TimeRange source;
    ClipRouting routing;
    RationalTime end() const { return position + source.duration; }
};
struct Layer {
    TrackId id;
    TrackKind kind;
    TrackPlayback playback;
    std::vector<LayerClip> clips;
};
struct Contribution {
    std::size_t layer, clip;
    RationalTime source;
    std::optional<std::uint32_t> stream;
    std::uint32_t gain_milli = 1000;
};
struct Evaluation {
    std::optional<Contribution> video;
    std::vector<Contribution> audio;
    RationalTime next_boundary;
};
// Immutable-by-convention detached plan. Layer order is top-to-bottom.
struct SequencePlan {
    SequenceId id;
    std::uint64_t revision;
    RationalTime duration, frame_duration;
    SequenceOutput output;
    std::vector<MediaAsset> media;
    std::vector<Layer> layers;
    Evaluation evaluate(RationalTime position) const;
};
SequencePlan make_sequence_plan(const ProjectSnapshot &project, SequenceId sequence);
} // namespace nle::playback
