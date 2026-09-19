#include "playback/sequence.hpp"
#include <algorithm>
#include <map>
namespace nle::playback {
SequencePlan make_sequence_plan(const ProjectSnapshot &project, SequenceId id) {
    validate(project);
    const auto sequence = std::find_if(project.sequences.begin(), project.sequences.end(),
                                       [&](const auto &value) { return value.id == id; });
    if (sequence == project.sequences.end())
        throw DomainError("Preview sequence not found");
    SequencePlan result{
        id, project.revision, {}, sequence->frame_duration, sequence->output, project.media, {}};
    std::map<MediaId, std::size_t> media;
    for (std::size_t i = 0; i < result.media.size(); ++i)
        media.emplace(result.media[i].id, i);
    for (const auto &track : sequence->tracks) {
        Layer layer{track.id, track.kind, track.playback, {}};
        for (const auto &clip : track.clips) {
            result.duration = std::max(result.duration, clip.position + clip.source.duration);
            layer.clips.push_back(
                {clip.id, media.at(clip.media), clip.position, clip.source, clip.routing});
        }
        result.layers.push_back(std::move(layer));
    }
    return result;
}
Evaluation SequencePlan::evaluate(RationalTime position) const {
    if (position > duration)
        throw DomainError("Preview seek is outside the sequence");
    Evaluation result{{}, {}, duration};
    if (position == duration)
        return result;
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const auto &layer = layers[i];
        if (!layer.playback.enabled)
            continue;
        const auto after = std::upper_bound(
            layer.clips.begin(), layer.clips.end(), position,
            [](RationalTime time, const auto &clip) { return time < clip.position; });
        if (after != layer.clips.end())
            result.next_boundary = std::min(result.next_boundary, after->position);
        if (after == layer.clips.begin())
            continue;
        const auto current = std::prev(after);
        if (position >= current->end())
            continue;
        result.next_boundary = std::min(result.next_boundary, current->end());
        const auto &asset = media[current->media];
        const auto clip = static_cast<std::size_t>(current - layer.clips.begin());
        const auto source = current->source.start + (position - current->position);
        if (!result.video && layer.kind == TrackKind::Video && asset.kind != MediaKind::Audio &&
            current->routing.video.mode != StreamMode::Disabled)
            result.video =
                Contribution{i, clip, source,
                             select_stream(asset, TrackKind::Video, current->routing.video), 1000};
        if (!layer.playback.muted && layer.playback.gain_milli != 0 &&
            asset.kind != MediaKind::Video && current->routing.audio.mode != StreamMode::Disabled)
            result.audio.push_back({i, clip, source,
                                    select_stream(asset, TrackKind::Audio, current->routing.audio),
                                    layer.playback.gain_milli});
    }
    return result;
}
} // namespace nle::playback
