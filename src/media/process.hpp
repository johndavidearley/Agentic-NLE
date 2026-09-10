#pragma once
#include "core/time.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace nle::media {
struct ProcessOptions {
    std::chrono::milliseconds timeout{30000};
    std::size_t max_output = 1024 * 1024;
    std::optional<std::reference_wrapper<const std::atomic_bool>> stop{};
};
std::string run_process(const std::filesystem::path &executable,
                        const std::vector<std::string> &arguments, ProcessOptions options = {});
std::filesystem::path utf8_path(const std::string &text);
std::string path_utf8(const std::filesystem::path &path);
} // namespace nle::media
