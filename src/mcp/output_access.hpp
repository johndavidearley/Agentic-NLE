#pragma once
#include "mcp/wire.hpp"
#include <filesystem>
namespace nle::mcp {
class OutputAccess {
  public:
    OutputAccess(std::vector<std::filesystem::path> roots, std::filesystem::path project);
    std::filesystem::path approved_file(const std::filesystem::path &candidate) const;

  private:
    std::vector<std::filesystem::path> roots_;
    std::filesystem::path project_;
};
} // namespace nle::mcp
