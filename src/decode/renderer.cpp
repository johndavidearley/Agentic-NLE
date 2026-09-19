#include "decode/renderer.hpp"
#include "media/probe.hpp"
#include <algorithm>
#include <cmath>
#include <set>
namespace nle::decode {
namespace {
std::filesystem::path path(const MediaAsset &asset) {
    const auto i = std::find_if(asset.locations.begin(), asset.locations.end(),
                                [](const auto &v) { return v.role == LocationRole::Original; });
    if (i == asset.locations.end())
        throw DomainError("Original source is unlocated");
    return media::utf8_path(i->uri);
}
} // namespace
std::size_t Chunk::bytes() const {
    auto n = audio.size() * sizeof(float);
    for (const auto &p : pictures)
        n += p.video.rgb.size();
    return n;
}
Renderer::Renderer(playback::SequencePlan plan, RationalTime start, std::atomic_bool &stop)
    : plan_(std::move(plan)), stop_(stop), video_(plan_.layers.size()),
      audio_(plan_.layers.size()) {
    (void)plan_.evaluate(start);
    if (plan_.frame_duration.value() > 1000000000 || plan_.frame_duration.rate() > 1000000000 ||
        plan_.duration > RationalTime{86400} || plan_.frame_duration < RationalTime{1, 60} ||
        plan_.frame_duration > RationalTime{1})
        throw DomainError("Preview supports up to 24 hours and output rates from 1 to 60 fps");
    const double scale = std::min(960.0 / plan_.output.width, 540.0 / plan_.output.height);
    width_ = std::max(2, static_cast<int>(plan_.output.width * scale));
    height_ = std::max(2, static_cast<int>(plan_.output.height * scale));
    unsigned video = 0, audio = 0;
    std::set<std::size_t> media;
    for (const auto &layer : plan_.layers) {
        if (layer.clips.empty() || !layer.playback.enabled)
            continue;
        if (layer.kind == TrackKind::Video)
            ++video;
        else
            ++audio;
        for (const auto &clip : layer.clips) {
            if (clip.source.end() > RationalTime{86400})
                throw DomainError("Preview source range exceeds 24 hours");
            const auto &asset = plan_.media[clip.media];
            const bool video_used = layer.kind == TrackKind::Video &&
                                    asset.kind != MediaKind::Audio &&
                                    clip.routing.video.mode != StreamMode::Disabled;
            const bool audio_used = !layer.playback.muted && layer.playback.gain_milli != 0 &&
                                    asset.kind != MediaKind::Video &&
                                    clip.routing.audio.mode != StreamMode::Disabled;
            if (video_used || audio_used)
                media.insert(clip.media);
        }
    }
    if (video > 2 || audio > 4)
        throw DomainError("Preview supports two populated video and four audio tracks");
    for (auto id : media) {
        const auto &asset = plan_.media[id];
        if (!asset.source || media::source_status(asset) != media::SourceStatus::Available)
            throw DomainError("Source missing or changed; relink before playback");
        identities_.push_back({id, std::filesystem::last_write_time(path(asset))});
    }
    sample_ = sample_ceil(start);
    // First output frame on/after the requested position. The separate poster is exact.
    const auto f = plan_.frame_duration;
    const auto seconds = static_cast<long double>(start.value()) / start.rate();
    frame_ = static_cast<std::int64_t>(seconds * f.rate() / f.value());
    while (frame_time() < start)
        ++frame_;
}
RationalTime Renderer::frame_time() const {
    return {frame_ * plan_.frame_duration.value(), plan_.frame_duration.rate()};
}
void Renderer::check_sources() const {
    for (const auto &entry : identities_) {
        const auto &asset = plan_.media[entry.media];
        if (media::source_status(asset) != media::SourceStatus::Available ||
            std::filesystem::last_write_time(path(asset)) != entry.modified)
            throw DomainError("Media source changed during playback; relink before continuing");
    }
}
Decoder &Renderer::decoder(const playback::Contribution &item, bool video) {
    if (!item.stream)
        throw DomainError("Selected clip has no matching source stream");
    const auto &clip = plan_.layers[item.layer].clips[item.clip];
    auto &slot = (video ? video_ : audio_)[item.layer];
    if (!slot.decoder || slot.media != clip.media || slot.stream != *item.stream) {
        slot.decoder.reset();
        slot.decoder = std::make_unique<Decoder>(plan_.media[clip.media], *item.stream, width_,
                                                 height_, stop_);
        slot.media = clip.media;
        slot.stream = *item.stream;
        slot.clip = {};
    }
    if (slot.clip != clip.id) {
        check_sources();
        slot.decoder->seek(item.source);
        slot.clip = clip.id;
    }
    return *slot.decoder;
}
Picture Renderer::poster(RationalTime time) {
    Picture picture{time, {}, {}};
    const auto evaluation = plan_.evaluate(time);
    if (evaluation.video) {
        const auto &item = *evaluation.video;
        picture.clip = plan_.layers[item.layer].clips[item.clip].id;
        picture.video = decoder(item, true).video(item.source);
    }
    if (picture.video.rgb.empty()) {
        picture.video.width = width_;
        picture.video.height = height_;
        picture.video.rgb.resize(static_cast<std::size_t>(width_ * height_ * 3));
    }
    return picture;
}
Chunk Renderer::next() {
    if (stop_.load())
        throw DomainError("Playback cancelled");
    if (blocks_++ % 100 == 0)
        check_sources();
    Chunk chunk;
    chunk.sample = sample_;
    const auto finish = sample_ceil(plan_.duration);
    if (sample_ >= finish) {
        chunk.end = true;
        return chunk;
    }
    const auto count = std::min<std::int64_t>(480, finish - sample_);
    chunk.audio.resize(static_cast<std::size_t>(count) * 2);
    auto at = sample_;
    while (at < sample_ + count) {
        const RationalTime time{at, 48000};
        const auto evaluation = plan_.evaluate(time);
        const auto end = std::min(sample_ + count, sample_ceil(evaluation.next_boundary));
        if (end <= at)
            throw DomainError("Invalid sequence boundary");
        auto output = std::span<float>(chunk.audio)
                          .subspan(static_cast<std::size_t>((at - sample_) * 2),
                                   static_cast<std::size_t>((end - at) * 2));
        for (const auto &item : evaluation.audio)
            decoder(item, false)
                .mix(sample_floor(item.source), output,
                     static_cast<float>(item.gain_milli) / 1000.0F);
        at = end;
    }
    for (auto &value : chunk.audio) {
        if (!std::isfinite(value))
            throw DomainError("Decoded audio contains a non-finite sample");
        value = std::clamp(value, -1.0F, 1.0F);
    }
    const RationalTime end_time{sample_ + count, 48000};
    while (frame_time() < end_time && frame_time() < plan_.duration) {
        chunk.pictures.push_back(poster(frame_time()));
        ++frame_;
    }
    sample_ += count;
    return chunk;
}
} // namespace nle::decode
