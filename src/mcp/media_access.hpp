#pragma once
#include "media/probe.hpp"
#include <filesystem>
#include <vector>
namespace nle::mcp {
class MediaAccess {
  public:
    MediaAccess(std::vector<std::filesystem::path> roots, std::filesystem::path probe_executable);
    const std::vector<std::filesystem::path> &roots() const { return roots_; }
    const std::filesystem::path &probe_executable() const { return probe_executable_; }
    std::filesystem::path approved_file(const std::filesystem::path &candidate) const;
    media::ProbeResult probe_file(const std::filesystem::path &candidate,
                                  media::ProcessOptions options = {}) const;

  private:
    std::vector<std::filesystem::path> roots_;
    std::filesystem::path probe_executable_;
};
} // namespace nle::mcp
