#pragma once
#include "commands/editor.hpp"
#include "media/process.hpp"
namespace nle::media {
struct ProbeResult {
    std::string uri;
    SourceMetadata source;
    RegisterMedia import_command() const;
    ReplaceMediaSource relink_command(MediaId id) const;
};
// Public parser permits deterministic adapter tests without an installed backend.
SourceMetadata parse_probe(std::string_view output, std::uint64_t byte_size);
ProbeResult probe(const std::filesystem::path &file,
                  const std::filesystem::path &executable = "ffprobe", ProcessOptions options = {});
enum class SourceStatus { Unlocated, Available, Missing, Unavailable, SizeChanged };
SourceStatus source_status(const MediaAsset &asset);
const char *status_name(SourceStatus status);
} // namespace nle::media
