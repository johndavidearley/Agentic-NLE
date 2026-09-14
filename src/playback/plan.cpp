#include "playback/plan.hpp"
#include <algorithm>
#include <limits>
namespace nle::playback {
std::int64_t milliseconds(RationalTime time) {
    if (time.value() > std::numeric_limits<std::int64_t>::max() / 1000)
        throw DomainError("preview time conversion overflow");
    return time.value() * 1000 / time.rate();
}
void validate_preview_source(const MediaAsset &asset) {
    if (!asset.source)
        throw DomainError("Preview requires probed source metadata; relink this asset first.");
    validate_source(*asset.source);
    int video = 0, audio = 0;
    for (const auto &stream : asset.source->streams) {
        if (stream.kind == TrackKind::Video)
            ++video;
        else
            ++audio;
        // Legacy assets retain independent stream coordinates after migration.
        // Re-import establishes a shared clock without silently moving existing edits.
        if (asset.source->time_mode == SourceTimeMode::LegacyPerStream &&
            ((stream.start_known && stream.start_ticks != 0) ||
             (!stream.start_known && asset.kind != MediaKind::Audio)))
            throw DomainError("Legacy preview requires zero, aligned stream starts. Re-import this "
                              "source as a new asset to use its shared source clock.");
    }
    if (video > 1 || audio > 1)
        throw DomainError(
            "Preview supports one video stream and one embedded audio stream per source.");
    if (std::none_of(asset.locations.begin(), asset.locations.end(),
                     [](const auto &location) { return location.role == LocationRole::Original; }))
        throw DomainError("Original source is unlocated; relink it before preview.");
}
Plan make_plan(const ProjectSnapshot &project, SequenceId id) {
    validate(project);
    const auto sequence = std::find_if(project.sequences.begin(), project.sequences.end(),
                                       [&](const auto &value) { return value.id == id; });
    if (sequence == project.sequences.end())
        throw DomainError("Preview sequence not found.");
    Plan result{id, project.revision, {}, {}};
    bool populated = false;
    for (const auto &track : sequence->tracks) {
        if (track.clips.empty())
            continue;
        if (populated)
            throw DomainError("This preview supports one populated track; multi-track mixing is a "
                              "later milestone.");
        populated = true;
        for (const auto &clip : track.clips) {
            const auto asset =
                std::find_if(project.media.begin(), project.media.end(),
                             [&](const auto &value) { return value.id == clip.media; });
            const auto end = clip.position + clip.source.duration;
            if (end > RationalTime{24 * 60 * 60} ||
                clip.source.end() > RationalTime{24 * 60 * 60} ||
                clip.source.duration < RationalTime{1, 1000} ||
                milliseconds(clip.source.end()) <= milliseconds(clip.source.start))
                throw DomainError(
                    "Preview supports timeline/source positions up to 24 hours and clips at least "
                    "one millisecond long.");
            result.segments.push_back({clip.id, clip.position, clip.source, *asset});
            result.duration = end;
        }
    }
    return result;
}
Sample Plan::sample(RationalTime position) const {
    if (position > duration)
        throw DomainError("Preview seek is outside the sequence.");
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const auto &segment = segments[i];
        if (position < segment.position)
            return {{}, {}, segment.position};
        if (position < segment.end())
            return {i, segment.source.start + (position - segment.position), segment.end()};
    }
    return {{}, {}, duration};
}
} // namespace nle::playback
