#pragma once
#include "playback/sequence.hpp"
#include <atomic>
#include <filesystem>
#include <functional>
namespace nle::exporting {
enum class Preset { LosslessReference, Mp4H264 };
struct Progress {
    std::uint64_t revision = 0;
    std::int64_t frames_complete = 0, frames_total = 0;
    std::int64_t samples_complete = 0, samples_total = 0;
};
struct Options {
    std::filesystem::path destination;
    Preset preset = Preset::LosslessReference;
    bool overwrite = false;
    std::function<void(const Progress &)> progress;
};
struct Result {
    std::uint64_t revision = 0;
    RationalTime duration;
    std::int64_t frames = 0, samples = 0;
    std::string video_codec, audio_codec, ffmpeg_version;
};
// The plan is detached from the live Editor and remains bound to its revision.
Result export_sequence(playback::SequencePlan plan, const Options &options, std::atomic_bool &stop);
} // namespace nle::exporting
