#pragma once
#include "media/probe.hpp"
namespace nle::media {
inline constexpr std::size_t max_index_frames = 200000;
inline constexpr std::size_t max_index_bytes = 16 * 1024 * 1024;
struct IndexedFrame {
    RationalTime position;
    RationalTime end;
    bool operator==(const IndexedFrame &) const = default;
};
struct FrameIndex {
    std::vector<IndexedFrame> frames;
    std::optional<std::size_t> at(RationalTime position) const;
    std::optional<RationalTime> step(RationalTime position, int direction, TimeRange range) const;
};
FrameIndex parse_frame_index(std::string_view text, const SourceMetadata &source);
FrameIndex index_frames(const std::filesystem::path &file, const SourceMetadata &source,
                        const std::filesystem::path &ffprobe = "ffprobe",
                        ProcessOptions options = {std::chrono::seconds(30), max_index_bytes});
} // namespace nle::media
