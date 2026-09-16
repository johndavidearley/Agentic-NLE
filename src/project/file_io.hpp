#pragma once
#include <filesystem>
#include <string_view>
namespace nle::detail {
// Same-directory staging and atomic replacement; no power-loss durability promise.
void atomic_write_file(std::string_view data, const std::filesystem::path &path);
} // namespace nle::detail
