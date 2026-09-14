#include "media/frame_index.hpp"
#include <algorithm>
#include <charconv>
#include <sstream>
namespace nle::media {
std::optional<std::size_t> FrameIndex::at(RationalTime position) const {
    const auto after = std::upper_bound(
        frames.begin(), frames.end(), position,
        [](RationalTime time, const IndexedFrame &frame) { return time < frame.position; });
    if (after == frames.begin())
        return {};
    const auto found = std::prev(after);
    if (position >= found->end)
        return {};
    return static_cast<std::size_t>(found - frames.begin());
}
std::optional<RationalTime> FrameIndex::step(RationalTime position, int direction,
                                             TimeRange range) const {
    if (direction != -1 && direction != 1)
        throw DomainError("frame step direction must be -1 or 1");
    if (frames.empty())
        return {};
    const auto current = at(position);
    if (direction > 0) {
        auto next = current
                        ? *current + 1
                        : static_cast<std::size_t>(
                              std::lower_bound(frames.begin(), frames.end(), position,
                                               [](const IndexedFrame &frame, RationalTime time) {
                                                   return frame.position < time;
                                               }) -
                              frames.begin());
        if (next < frames.size() && frames[next].position < range.end())
            return std::max(range.start, frames[next].position);
    } else {
        const auto before =
            current ? frames.begin() + static_cast<std::ptrdiff_t>(*current)
                    : std::lower_bound(frames.begin(), frames.end(), position,
                                       [](const IndexedFrame &frame, RationalTime time) {
                                           return frame.position < time;
                                       });
        if (before != frames.begin()) {
            const auto &previous = *std::prev(before);
            if (previous.end > range.start)
                return std::max(range.start, previous.position);
        }
    }
    return {};
}
FrameIndex parse_frame_index(std::string_view text, const SourceMetadata &source) {
    validate_source(source);
    if (text.size() > max_index_bytes)
        throw DomainError("frame index exceeds byte limit");
    const SourceStream *video = nullptr;
    for (const auto &stream : source.streams)
        if (stream.kind == TrackKind::Video) {
            if (video)
                throw DomainError("frame indexing requires one video stream");
            video = &stream;
        }
    if (!video)
        return {};
    const auto origin = source_origin(source);
    FrameIndex result;
    std::istringstream input{std::string(text)};
    std::string line;
    RationalTime last_duration;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        if (!line.starts_with("frame|") || line.size() > 4096)
            throw DomainError("invalid frame index record");
        std::optional<std::int64_t> pts, duration;
        std::istringstream fields(line.substr(6));
        std::string field;
        while (std::getline(fields, field, '|')) {
            const auto equal = field.find('=');
            if (equal == std::string::npos)
                throw DomainError("invalid frame index field");
            const auto key = field.substr(0, equal), value = field.substr(equal + 1);
            if (key != "best_effort_timestamp" && key != "duration" && key != "pkt_duration")
                continue;
            if (value == "N/A")
                continue;
            std::int64_t number{};
            const auto [end, error] =
                std::from_chars(value.data(), value.data() + value.size(), number);
            if (error != std::errc{} || end != value.data() + value.size())
                throw DomainError("invalid decoded frame timestamp");
            if (key == "best_effort_timestamp") {
                if (pts)
                    throw DomainError("duplicate frame timestamp");
                pts = number;
            } else if (!duration || key == "duration")
                duration = number;
        }
        if (!pts)
            throw DomainError("decoded video frame has no timestamp");
        const auto position = SourceTime::from_ticks(*pts, video->time_base).since(origin);
        if (!result.frames.empty()) {
            if (position <= result.frames.back().position)
                throw DomainError("decoded frame timestamps are not strictly increasing");
            result.frames.back().end = position;
        }
        if (result.frames.size() == max_index_frames)
            throw DomainError("frame index exceeds frame limit");
        if (duration && *duration <= 0)
            throw DomainError("invalid decoded frame duration");
        last_duration = duration ? SourceTime::from_ticks(*duration, video->time_base).since({})
                                 : RationalTime{};
        result.frames.push_back({position, position});
    }
    if (result.frames.empty() || last_duration == RationalTime{})
        throw DomainError("complete frame index requires frames and a final frame duration");
    result.frames.back().end = result.frames.back().position + last_duration;
    return result;
}
FrameIndex index_frames(const std::filesystem::path &file, const SourceMetadata &source,
                        const std::filesystem::path &ffprobe, ProcessOptions options) {
    validate_source(source);
    const auto video =
        std::find_if(source.streams.begin(), source.streams.end(),
                     [](const auto &stream) { return stream.kind == TrackKind::Video; });
    if (video == source.streams.end())
        return {};
    const auto path = std::filesystem::canonical(file);
    const auto size = std::filesystem::file_size(path);
    const auto modified = std::filesystem::last_write_time(path);
    if (size != source.byte_size)
        throw DomainError("source changed before frame indexing");
    options.max_output = std::min(options.max_output, max_index_bytes);
    const auto output = run_process(
        ffprobe,
        {"-v", "error", "-protocol_whitelist", "file", "-format_whitelist",
         "wav,mov,matroska,avi,flac,mp3,ogg,aiff", "-select_streams", std::to_string(video->index),
         "-show_entries", "frame=best_effort_timestamp,duration,pkt_duration", "-of",
         "compact=p=1:nk=0", path_utf8(path)},
        options);
    if (size != std::filesystem::file_size(path) ||
        modified != std::filesystem::last_write_time(path))
        throw DomainError("source changed during frame indexing");
    return parse_frame_index(output, source);
}
} // namespace nle::media
