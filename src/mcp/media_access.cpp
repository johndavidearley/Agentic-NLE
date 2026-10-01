#include "mcp/media_access.hpp"
#include "mcp/wire.hpp"
#include <algorithm>
namespace nle::mcp {
namespace {
std::filesystem::path existing(const std::filesystem::path &path, bool directory) {
    if (!path.is_absolute())
        throw Failure("configuration", "Media roots and probe executable must be absolute paths.");
    std::error_code error;
    const auto canonical = std::filesystem::canonical(path, error);
    if (error ||
        (directory ? !std::filesystem::is_directory(canonical, error)
                   : !std::filesystem::is_regular_file(canonical, error)) ||
        error)
        throw Failure("configuration", "Media root or probe executable is unavailable.");
    return canonical;
}
bool within(const std::filesystem::path &file, const std::filesystem::path &root) {
    const auto [parent_end, file_end] =
        std::mismatch(root.begin(), root.end(), file.begin(), file.end());
    (void)file_end;
    return parent_end == root.end();
}
} // namespace
MediaAccess::MediaAccess(std::vector<std::filesystem::path> roots,
                         std::filesystem::path probe_executable)
    : probe_executable_(existing(probe_executable, false)) {
    if (roots.empty() || roots.size() > 8)
        throw Failure("configuration", "Select between one and eight approved media roots.");
    for (const auto &root : roots) {
        const auto canonical = existing(root, true);
        if (canonical == canonical.root_path())
            throw Failure("configuration", "A filesystem root cannot be a media root.");
        if (std::find(roots_.begin(), roots_.end(), canonical) == roots_.end())
            roots_.push_back(canonical);
    }
}
std::filesystem::path MediaAccess::approved_file(const std::filesystem::path &candidate) const {
    if (!candidate.is_absolute())
        throw Failure("media_path_denied",
                      "Media path must be absolute and inside an approved root.");
    std::error_code error;
    const auto canonical = std::filesystem::canonical(candidate, error);
    if (error || !std::filesystem::is_regular_file(canonical, error) || error)
        throw Failure("media_unavailable", "Media file is missing or unavailable.");
    if (std::none_of(roots_.begin(), roots_.end(),
                     [&](const auto &root) { return within(canonical, root); }))
        throw Failure("media_path_denied", "Media path is outside the approved roots.");
    return canonical;
}
media::ProbeResult MediaAccess::probe_file(const std::filesystem::path &candidate,
                                           media::ProcessOptions options) const {
    const auto approved = approved_file(candidate);
    auto result = media::probe(approved, probe_executable_, options);
    if (approved_file(media::utf8_path(result.uri)) != approved)
        throw Failure("media_path_denied", "Media path changed while probing.");
    return result;
}
} // namespace nle::mcp
