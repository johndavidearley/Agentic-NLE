#pragma once
#include "project/persistence.hpp"
#include <optional>
namespace nle {
struct FileError : DomainError {
    std::string code;
    FileError(std::string value, const std::string &message)
        : DomainError(message), code(std::move(value)) {}
};
struct RecoveryResult {
    std::optional<ProjectSnapshot> project;
    std::string warning;
};
// Application-level document ownership. Raw persistence remains usable for fixtures/adapters.
class DocumentFile {
  public:
    DocumentFile(std::filesystem::path path, bool writable, bool allow_missing = false);
    ~DocumentFile();
    DocumentFile(const DocumentFile &) = delete;
    DocumentFile &operator=(const DocumentFile &) = delete;
    const std::filesystem::path &path() const { return path_; }
    bool exists() const { return !bytes_.empty(); }
    bool matches(const std::filesystem::path &path) const;
    ProjectSnapshot load() const;
    void save(const ProjectSnapshot &project);
    void checkpoint(const ProjectSnapshot &project);
    RecoveryResult recovery() const;
    ProjectSnapshot recover();
    void discard_recovery();
    const std::string &cleanup_warning() const { return cleanup_warning_; }
    std::filesystem::path recovery_path(unsigned slot) const;

  private:
    std::filesystem::path path_;
    bool writable_, recovery_accepted_ = false;
    std::string bytes_, cleanup_warning_;
#ifdef _WIN32
    void *lock_ = nullptr;
#else
    int lock_ = -1;
#endif
    void unlock() noexcept;
    void unchanged() const;
    void require_recovery_choice() const;
    std::optional<ProjectSnapshot> read_checkpoint(unsigned slot) const;
};
} // namespace nle
