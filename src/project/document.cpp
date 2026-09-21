#include "project/document.hpp"
#include "project/file_io.hpp"
#include <fstream>
#include <sstream>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace nle {
namespace {
constexpr std::size_t native_limit = 16 * 1024 * 1024;
std::string read_bytes(const std::filesystem::path &path, std::size_t limit) {
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path)))
        throw FileError("project_unavailable", "Expected a regular file, not a link or directory.");
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw FileError("project_unavailable", "Cannot read the configured file.");
    const auto length = input.tellg();
    if (length < 0 || static_cast<std::uintmax_t>(length) > limit)
        throw FileError("project_limit", "File exceeds the supported size limit.");
    std::string bytes(static_cast<std::size_t>(length), '\0');
    input.seekg(0);
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
        throw FileError("project_unavailable", "Cannot read the configured file.");
    return bytes;
}
bool present(const std::filesystem::path &path) {
    // symlink_status also detects dangling links; they are never treated as empty destinations.
    const auto status = std::filesystem::symlink_status(path);
    return status.type() != std::filesystem::file_type::not_found;
}
} // namespace
DocumentFile::DocumentFile(std::filesystem::path path, bool writable, bool allow_missing)
    : writable_(writable) {
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    if (present(absolute))
        path_ = std::filesystem::canonical(absolute);
    else if (allow_missing)
        path_ = std::filesystem::canonical(absolute.parent_path()) / absolute.filename();
    else
        throw FileError("project_unavailable", "Select an existing native project file.");
    try {
        if (writable_) {
            auto sidecar = path_;
            // Keep the M6 name so old MCP servers and all new clients share ownership.
            sidecar += ".mcp-lock";
#ifdef _WIN32
            const auto handle =
                CreateFileW(sidecar.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
                throw FileError(
                    "project_locked",
                    "Another editor may own this project, or its folder is not writable.");
            lock_ = handle;
            BY_HANDLE_FILE_INFORMATION information{};
            if (!GetFileInformationByHandle(handle, &information) ||
                (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                throw FileError("project_locked", "Cannot safely acquire the project lock.");
#else
            lock_ = ::open(sidecar.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
            struct stat information{};
            if (lock_ < 0 || ::fstat(lock_, &information) != 0 || !S_ISREG(information.st_mode) ||
                ::flock(lock_, LOCK_EX | LOCK_NB) != 0)
                throw FileError(
                    "project_locked",
                    "Another editor may own this project, or its folder is not writable.");
#endif
        }
        if (present(path_)) {
            bytes_ = read_bytes(path_, native_limit);
            (void)deserialize(bytes_);
        } else if (!allow_missing)
            throw FileError("project_unavailable",
                            "Project disappeared before it could be opened.");
    } catch (...) {
        unlock();
        throw;
    }
}
void DocumentFile::unlock() noexcept {
#ifdef _WIN32
    if (lock_) {
        CloseHandle(lock_);
        lock_ = nullptr;
    }
#else
    if (lock_ >= 0) {
        ::close(lock_);
        lock_ = -1;
    }
#endif
}
DocumentFile::~DocumentFile() { unlock(); }
bool DocumentFile::matches(const std::filesystem::path &path) const {
    std::error_code error;
    const auto normalized = std::filesystem::weakly_canonical(path, error);
    if (!error && normalized == path_)
        return true;
#ifdef _WIN32
    return std::filesystem::equivalent(path_, path, error) && !error;
#else
    return false;
#endif
}
ProjectSnapshot DocumentFile::load() const {
    if (bytes_.empty())
        throw FileError("project_unavailable", "This document has not been saved yet.");
    return deserialize(bytes_);
}
void DocumentFile::unchanged() const {
    if (!writable_)
        throw FileError("permission_denied", "This document has no write permission.");
    try {
        if (std::filesystem::canonical(path_.parent_path()) != path_.parent_path() ||
            (bytes_.empty() ? present(path_) : read_bytes(path_, native_limit) != bytes_))
            throw DomainError("changed");
    } catch (...) {
        throw FileError(
            "save_conflict",
            "The project changed or became unavailable outside this session. Save was rejected.");
    }
}
void DocumentFile::save(const ProjectSnapshot &project) {
    require_recovery_choice();
    auto next = serialize(project);
    unchanged();
    try {
        detail::atomic_write_file(next, path_);
    } catch (...) {
        throw FileError("save_failed",
                        "Could not replace the project file. Unsaved work is still available.");
    }
    bytes_ = std::move(next);
    cleanup_warning_.clear();
    try {
        discard_recovery();
    } catch (const std::exception &) {
        // The save already succeeded. Never describe it as a failed/retryable write.
        cleanup_warning_ = "Project saved, but old recovery files could not be removed.";
    }
}
std::filesystem::path DocumentFile::recovery_path(unsigned slot) const {
    if (slot > 1)
        throw DomainError("invalid recovery slot");
    auto result = path_;
    result += ".recovery-" + std::to_string(slot);
    return result;
}
std::optional<ProjectSnapshot> DocumentFile::read_checkpoint(unsigned slot) const {
    const auto file = recovery_path(slot);
    if (!present(file))
        return {};
    const auto bytes = read_bytes(file, 2 * native_limit + 128);
    const auto newline = bytes.find('\n');
    if (newline == std::string::npos || newline > 100)
        throw DomainError("invalid recovery header");
    std::istringstream header(bytes.substr(0, newline));
    std::string magic, extra;
    unsigned version = 0;
    std::size_t base_size = 0, snapshot_size = 0;
    if (!(header >> magic >> version >> base_size >> snapshot_size) || magic != "NLE_RECOVERY" ||
        version != 1 || (header >> extra) || base_size > native_limit ||
        snapshot_size > native_limit || bytes.size() != newline + 1 + base_size + snapshot_size)
        throw DomainError("invalid recovery envelope");
    const auto payload = std::string_view(bytes).substr(newline + 1);
    if (payload.substr(0, base_size) != bytes_)
        throw DomainError("recovery belongs to another saved version");
    auto project = deserialize(payload.substr(base_size));
    if (!bytes_.empty()) {
        const auto base = deserialize(bytes_);
        if (project.id != base.id || project.revision < base.revision)
            throw DomainError("recovery identity or revision does not match");
        if (project == base)
            return {};
    }
    return project;
}
RecoveryResult DocumentFile::recovery() const {
    RecoveryResult result;
    for (unsigned slot = 0; slot < 2; ++slot) {
        try {
            auto candidate = read_checkpoint(slot);
            if (candidate && (!result.project || candidate->revision > result.project->revision))
                result.project = std::move(candidate);
        } catch (const std::exception &) {
            result.warning = "A recovery checkpoint is damaged, unavailable or belongs to another "
                             "saved version.";
        }
    }
    return result;
}
ProjectSnapshot DocumentFile::recover() {
    auto pending = recovery();
    if (!pending.project)
        throw FileError("recovery_unavailable",
                        "No matching valid recovery checkpoint is available.");
    recovery_accepted_ = true;
    return std::move(*pending.project);
}
void DocumentFile::require_recovery_choice() const {
    if (!recovery_accepted_) {
        const auto pending = recovery();
        if (pending.project || !pending.warning.empty())
            throw FileError("recovery_available", "Recovery files exist. Recover or explicitly "
                                                  "discard them before writing this project.");
    }
}
void DocumentFile::checkpoint(const ProjectSnapshot &project) {
    require_recovery_choice();
    const auto next = serialize(project);
    unchanged();
    if (!bytes_.empty()) {
        const auto base = deserialize(bytes_);
        if (project.id != base.id || project.revision < base.revision)
            throw FileError("recovery_failed",
                            "Recovery identity or revision does not match the saved project.");
        if (next == serialize(base))
            return;
    }
    std::optional<ProjectSnapshot> slots[2];
    for (unsigned slot = 0; slot < 2; ++slot) {
        try {
            slots[slot] = read_checkpoint(slot);
        } catch (const std::exception &) {
        }
    }
    // Never replace the only/newest valid checkpoint when updating the other slot.
    unsigned target = 0;
    if (slots[0] && (!slots[1] || slots[0]->revision > slots[1]->revision))
        target = 1;
    const auto latest = recovery().project;
    if (latest && *latest == project)
        return;
    const auto destination = recovery_path(target);
    if (present(destination) &&
        !std::filesystem::is_regular_file(std::filesystem::symlink_status(destination)))
        throw FileError("recovery_failed", "Recovery destination is not a regular file.");
    const auto data = "NLE_RECOVERY 1 " + std::to_string(bytes_.size()) + " " +
                      std::to_string(next.size()) + "\n" + bytes_ + next;
    try {
        detail::atomic_write_file(data, destination);
        recovery_accepted_ = true;
    } catch (...) {
        throw FileError("recovery_failed", "Cannot write a recovery checkpoint. The live project "
                                           "and previous checkpoint remain available.");
    }
}
void DocumentFile::discard_recovery() {
    if (!writable_)
        throw FileError("permission_denied", "This document has no recovery write permission.");
    for (unsigned slot = 0; slot < 2; ++slot) {
        const auto file = recovery_path(slot);
        if (!present(file))
            continue;
        if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(file)))
            throw FileError("recovery_failed", "Recovery destination is not a regular file.");
        std::filesystem::remove(file);
    }
    recovery_accepted_ = true;
}
} // namespace nle
