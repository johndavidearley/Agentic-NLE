#pragma once
#include "project/model.hpp"
#include <filesystem>
#include <string_view>

namespace nle {
// Persistence contains domain state and the ID watermark, never session undo history.
std::string serialize(const ProjectSnapshot &project);
ProjectSnapshot deserialize(std::string_view data);
void save_project(const ProjectSnapshot &project, const std::filesystem::path &path);
ProjectSnapshot load_project(const std::filesystem::path &path);
} // namespace nle
