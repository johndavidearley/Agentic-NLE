#include "media/probe.hpp"
#include <charconv>
#include <map>
#include <sstream>
namespace nle::media {
namespace {
using Fields = std::map<std::string, std::string>;
template <typename T> T integer(const std::string &text) {
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
        throw DomainError("invalid probe integer");
    return value;
}
std::string required(const Fields &fields, const std::string &key) {
    const auto it = fields.find(key);
    if (it == fields.end() || it->second.empty() || it->second == "N/A")
        throw DomainError("missing probe field: " + key);
    return it->second;
}
std::string optional(const Fields &fields, const std::string &key) {
    const auto it = fields.find(key);
    return it == fields.end() || it->second == "N/A" ? "" : it->second;
}
RationalTime fraction(const std::string &text, bool inverse = false) {
    if (text.empty() || text == "0/0")
        return {};
    const auto slash = text.find('/');
    if (slash == std::string::npos)
        throw DomainError("invalid probe fraction");
    const auto numerator = integer<std::int64_t>(text.substr(0, slash));
    const auto denominator = integer<std::int64_t>(text.substr(slash + 1));
    if (inverse && numerator == 0 && denominator > 0)
        return {};
    return inverse ? RationalTime{denominator, numerator} : RationalTime{numerator, denominator};
}
RationalTime decimal(const std::string &text) {
    if (text.empty())
        return {};
    const auto dot = text.find('.');
    if (dot == std::string::npos)
        return {integer<std::int64_t>(text)};
    const auto places = text.size() - dot - 1;
    if (places == 0 || places > 9 || dot == 0)
        throw DomainError("invalid probe decimal duration");
    auto digits = text;
    digits.erase(dot, 1);
    std::int64_t scale = 1;
    for (std::size_t i = 0; i < places; ++i)
        scale *= 10;
    return {integer<std::int64_t>(digits), scale};
}
} // namespace
SourceMetadata parse_probe(std::string_view output, std::uint64_t byte_size) {
    if (output.size() > 1024 * 1024)
        throw DomainError("probe output too large");
    std::istringstream input{std::string(output)};
    SourceMetadata result;
    result.byte_size = byte_size;
    std::string section, line;
    Fields fields;
    bool version_seen = false, format_seen = false;
    std::size_t stream_count = 0;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        if (line.size() > 8192)
            throw DomainError("probe line too long");
        if (line.front() == '[') {
            if (line == "[/" + section + "]" && !section.empty()) {
                if (section == "PROGRAM_VERSION") {
                    if (version_seen)
                        throw DomainError("duplicate probe version");
                    version_seen = true;
                    result.probe_version = required(fields, "version");
                    result.probe_configuration = optional(fields, "configuration");
                    if (result.probe_configuration.empty())
                        result.probe_configuration = "unspecified";
                } else if (section == "FORMAT") {
                    if (format_seen)
                        throw DomainError("duplicate probe format");
                    format_seen = true;
                    result.container = required(fields, "format_name");
                    result.container_duration = decimal(optional(fields, "duration"));
                } else {
                    if (++stream_count > 64)
                        throw DomainError("too many probe streams");
                    const auto kind = required(fields, "codec_type");
                    const auto attached = optional(fields, "DISPOSITION:attached_pic");
                    if (!attached.empty() && attached != "0" && attached != "1")
                        throw DomainError("invalid attached picture flag");
                    if ((kind == "video" || kind == "audio") && attached != "1") {
                        SourceStream stream;
                        stream.index = integer<std::uint32_t>(required(fields, "index"));
                        stream.kind = kind == "video" ? TrackKind::Video : TrackKind::Audio;
                        stream.codec = required(fields, "codec_name");
                        stream.time_base = fraction(required(fields, "time_base"));
                        const auto ticks = optional(fields, "duration_ts");
                        if (!ticks.empty())
                            stream.duration_ticks = integer<std::int64_t>(ticks);
                        const auto start = optional(fields, "start_pts");
                        stream.start_known = !start.empty();
                        if (stream.start_known)
                            stream.start_ticks = integer<std::int64_t>(start);
                        if (stream.kind == TrackKind::Video) {
                            stream.width = integer<std::uint32_t>(required(fields, "width"));
                            stream.height = integer<std::uint32_t>(required(fields, "height"));
                            stream.frame_duration =
                                fraction(optional(fields, "avg_frame_rate"), true);
                            stream.nominal_frame_duration =
                                fraction(optional(fields, "r_frame_rate"), true);
                        } else {
                            stream.sample_rate =
                                integer<std::uint32_t>(required(fields, "sample_rate"));
                            stream.channels = integer<std::uint32_t>(required(fields, "channels"));
                        }
                        result.streams.push_back(std::move(stream));
                    }
                }
                section.clear();
                fields.clear();
            } else {
                if (!section.empty() ||
                    (line != "[STREAM]" && line != "[FORMAT]" && line != "[PROGRAM_VERSION]"))
                    throw DomainError("unexpected probe section");
                section = line.substr(1, line.size() - 2);
            }
        } else {
            const auto equal = line.find('=');
            if (section.empty() || equal == std::string::npos || equal == 0 ||
                !fields.emplace(line.substr(0, equal), line.substr(equal + 1)).second)
                throw DomainError("invalid or duplicate probe field");
        }
    }
    if (!section.empty() || !version_seen || !format_seen)
        throw DomainError("incomplete probe output");
    validate_source(result);
    return result;
}
ProbeResult probe(const std::filesystem::path &file, const std::filesystem::path &executable,
                  ProcessOptions options) {
    std::error_code error;
    const auto path = std::filesystem::canonical(file, error);
    if (error || !std::filesystem::is_regular_file(path, error) || error)
        throw DomainError("media source is missing, inaccessible, or not a regular file");
    const auto uri = path_utf8(path);
    validate_text(uri);
    const auto size = std::filesystem::file_size(path);
    const auto modified = std::filesystem::last_write_time(path);
    const auto output = run_process(
        executable,
        {"-v", "error", "-protocol_whitelist", "file", "-format_whitelist",
         "wav,mov,matroska,avi,flac,mp3,ogg,aiff", "-probesize", "5000000", "-analyzeduration",
         "5000000", "-show_program_version", "-show_entries",
         "program_version=version,configuration:stream=index,codec_name,codec_type,time_base,"
         "duration_ts,start_pts,avg_frame_rate,r_frame_rate,width,height,sample_rate,channels:"
         "stream_disposition=attached_pic:format=format_name,duration",
         "-of", "default", uri},
        options);
    if (size != std::filesystem::file_size(path) ||
        modified != std::filesystem::last_write_time(path))
        throw DomainError("media source changed during probing");
    return {uri, parse_probe(output, size)};
}
RegisterMedia ProbeResult::import_command() const {
    validate_source(source);
    return {path_utf8(utf8_path(uri).filename()),
            source_kind(source),
            source_duration(source),
            {{LocationRole::Original, uri}},
            source};
}
ReplaceMediaSource ProbeResult::relink_command(MediaId id) const { return {id, uri, source}; }
SourceStatus source_status(const MediaAsset &asset) {
    for (const auto &location : asset.locations) {
        if (location.role != LocationRole::Original)
            continue;
        std::error_code error;
        const auto path = utf8_path(location.uri);
        if (!std::filesystem::exists(path, error))
            return error ? SourceStatus::Unavailable : SourceStatus::Missing;
        if (!std::filesystem::is_regular_file(path, error) || error)
            return SourceStatus::Unavailable;
        const auto size = std::filesystem::file_size(path, error);
        if (error)
            return SourceStatus::Unavailable;
        if (asset.source && size != asset.source->byte_size)
            return SourceStatus::SizeChanged;
        return SourceStatus::Available;
    }
    return SourceStatus::Unlocated;
}
const char *status_name(SourceStatus status) {
    switch (status) {
    case SourceStatus::Unlocated:
        return "unlocated";
    case SourceStatus::Available:
        return "available";
    case SourceStatus::Missing:
        return "missing";
    case SourceStatus::Unavailable:
        return "unavailable";
    case SourceStatus::SizeChanged:
        return "size-changed";
    }
    return "unknown";
}
} // namespace nle::media
