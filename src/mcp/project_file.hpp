#pragma once
#include "mcp/wire.hpp"
#include <filesystem>
namespace nle::mcp {
class ProjectFile {
  public:
    ProjectFile(std::filesystem::path path, bool allow_save);
    ~ProjectFile();
    ProjectFile(const ProjectFile &) = delete;
    ProjectFile &operator=(const ProjectFile &) = delete;
    ProjectSnapshot load() const;
    void save(const ProjectSnapshot &project);

  private:
    std::filesystem::path path_;
    std::string bytes_;
    bool allow_save_;
#ifdef _WIN32
    void *lock_ = nullptr;
#else
    int lock_ = -1;
#endif
    void unlock() noexcept;
};
} // namespace nle::mcp
