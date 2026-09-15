#include "mcp/project_file.hpp"
#include "project/persistence.hpp"
#include <fstream>
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
namespace nle::mcp {
namespace {
std::string read_bytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw Failure("project_unavailable", "Cannot read the configured project.");
    const auto size = input.tellg();
    if (size < 0 || size > 16 * 1024 * 1024)
        throw Failure("project_limit", "Configured project exceeds the native file limit.");
    std::string bytes(static_cast<std::size_t>(size), '\0');
    input.seekg(0);
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
        throw Failure("project_unavailable", "Cannot read the configured project.");
    return bytes;
}
} // namespace
ProjectFile::ProjectFile(std::filesystem::path path, bool allow_save) : allow_save_(allow_save) {
    std::error_code error;
    path_ = std::filesystem::canonical(path, error);
    if (error || !std::filesystem::is_regular_file(path_, error) || error)
        throw Failure("project_unavailable", "Select an existing native project file.");
    try {
        if (allow_save_) {
            auto sidecar = path_;
            sidecar += ".mcp-lock";
#ifdef _WIN32
            const auto handle =
                CreateFileW(sidecar.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
                throw Failure("project_locked",
                              "Another saving session may already own this project.");
            lock_ = handle;
            BY_HANDLE_FILE_INFORMATION information{};
            if (!GetFileInformationByHandle(handle, &information) ||
                (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                throw Failure("project_locked", "Cannot safely acquire the project sidecar lock.");
#else
            lock_ = ::open(sidecar.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
            struct stat information{};
            if (lock_ < 0 || ::fstat(lock_, &information) != 0 || !S_ISREG(information.st_mode) ||
                ::flock(lock_, LOCK_EX | LOCK_NB) != 0)
                throw Failure("project_locked",
                              "Another saving session may already own this project.");
#endif
        }
        bytes_ = read_bytes(path_);
    } catch (...) {
        unlock();
        throw;
    }
}
void ProjectFile::unlock() noexcept {
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
ProjectFile::~ProjectFile() { unlock(); }
ProjectSnapshot ProjectFile::load() const { return deserialize(bytes_); }
void ProjectFile::save(const ProjectSnapshot &project) {
    if (!allow_save_)
        throw Failure("permission_denied", "This session has no save permission.");
    auto next = serialize(project);
    try {
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(path_)) ||
            std::filesystem::canonical(path_) != path_ || read_bytes(path_) != bytes_)
            throw Failure("save_conflict",
                          "The project file changed outside this session. Save was rejected.");
    } catch (...) {
        throw Failure("save_conflict",
                      "The project file changed or became unavailable. Save was rejected.");
    }
    try {
        save_project(project, path_);
    } catch (...) {
        throw Failure(
            "save_failed",
            "Could not replace the configured project file. Session state is still available.");
    }
    bytes_ = std::move(next);
}
} // namespace nle::mcp
