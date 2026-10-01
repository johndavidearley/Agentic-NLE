#include "mcp/output_access.hpp"
#include "media/probe.hpp"
#include <algorithm>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace nle::mcp {
OutputAccess::OutputAccess(std::vector<std::filesystem::path> roots, std::filesystem::path project)
    : project_(std::filesystem::canonical(project)) {
    if (roots.empty() || roots.size() > 8)
        throw Failure("configuration", "Select one to eight output roots.");
    for (const auto &root : roots) {
        std::error_code error;
        const auto canonical = std::filesystem::canonical(root, error);
        if (!root.is_absolute() || error || !std::filesystem::is_directory(canonical) ||
            canonical == canonical.root_path())
            throw Failure("configuration",
                          "Output roots must be absolute existing non-root directories.");
        roots_.push_back(canonical);
    }
}
std::filesystem::path OutputAccess::approved_file(const std::filesystem::path &candidate) const {
    const auto denied = [] {
        throw Failure("output_path_denied",
                      "Select a regular output file inside an approved output root, distinct from "
                      "the project and its sidecars.");
    };
    if (!candidate.is_absolute() || candidate.filename().empty() || candidate.filename() == "." ||
        candidate.filename() == "..")
        denied();
#ifdef _WIN32
    if (candidate.filename().native().find(L':') != std::wstring::npos)
        denied(); // Alternate data streams are not regular output destinations.
#endif
    std::error_code error;
    const auto parent = std::filesystem::canonical(candidate.parent_path(), error);
    if (error || !std::filesystem::is_directory(parent))
        denied();
    const auto path = parent / candidate.filename();
    if (std::none_of(roots_.begin(), roots_.end(), [&](const auto &root) {
            return std::mismatch(root.begin(), root.end(), parent.begin(), parent.end()).first ==
                   root.end();
        }))
        denied();
    const auto status = std::filesystem::symlink_status(path, error);
    if (error && error != std::errc::no_such_file_or_directory)
        denied();
    if (std::filesystem::exists(status) && !std::filesystem::is_regular_file(status))
        denied();
    // Reserve the selected native project and all sibling names sharing its prefix.
    bool reserved =
        media::path_utf8(path.filename()).starts_with(media::path_utf8(project_.filename()));
#ifdef _WIN32
    const auto name = path.filename().native(), prefix = project_.filename().native();
    reserved = name.size() >= prefix.size() &&
               CompareStringOrdinal(name.data(), static_cast<int>(prefix.size()), prefix.data(),
                                    static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
#endif
    if (std::filesystem::equivalent(parent, project_.parent_path(), error) && reserved)
        denied();
    if (std::filesystem::exists(status) && std::filesystem::equivalent(path, project_, error))
        denied();
    return path;
}
} // namespace nle::mcp
