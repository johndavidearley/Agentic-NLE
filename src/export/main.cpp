#include "export/export.hpp"
#include "media/process.hpp"
#include "project/persistence.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <csignal>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
std::atomic_bool cancelled = false;
static_assert(std::atomic_bool::is_always_lock_free);
void cancel(int) { cancelled.store(true, std::memory_order_relaxed); }
int run(int argc, char **argv) {
    if (argc != 4 && argc != 5) {
        std::cerr
            << "Usage: editor-export PROJECT SEQUENCE_ID OUTPUT.mkv|OUTPUT.mp4 [--overwrite]\n";
        return 2;
    }
    try {
        std::uint64_t id = 0;
        const std::string_view text = argv[2];
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), id);
        if (error != std::errc{} || end != text.data() + text.size() || id == 0)
            throw nle::DomainError("sequence ID must be a positive integer");
        const bool overwrite = argc == 5 && std::string_view(argv[4]) == "--overwrite";
        if (argc == 5 && !overwrite)
            throw nle::DomainError("the only supported option is --overwrite");
        auto preset = nle::exporting::Preset::LosslessReference;
        const auto output = nle::media::utf8_path(argv[3]);
        auto extension = output.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (extension == ".mp4")
            preset = nle::exporting::Preset::Mp4H264;
        std::signal(SIGINT, cancel);
        std::signal(SIGTERM, cancel);
        auto project = nle::load_project(nle::media::utf8_path(argv[1]));
        auto plan = nle::playback::make_sequence_plan(project, nle::SequenceId{id});
        std::int64_t last_second = -1;
        nle::exporting::Options options{
            output, preset, overwrite, [&](const nle::exporting::Progress &progress) {
                const auto second = progress.samples_complete / 48000;
                if (second != last_second || progress.samples_complete == progress.samples_total) {
                    last_second = second;
                    std::cerr << "Export revision " << progress.revision << ": "
                              << progress.frames_complete << '/' << progress.frames_total
                              << " frames, " << progress.samples_complete << '/'
                              << progress.samples_total << " samples\n";
                }
            }};
        const auto result = nle::exporting::export_sequence(std::move(plan), options, cancelled);
        std::cout << "Exported revision=" << result.revision << " frames=" << result.frames
                  << " samples=" << result.samples << " video=" << result.video_codec
                  << " audio=" << result.audio_codec << " ffmpeg=" << result.ffmpeg_version << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "editor-export: " << error.what() << '\n';
        return 1;
    }
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t **wide) {
    std::vector<std::string> storage;
    for (int i = 0; i < argc; ++i)
        storage.push_back(nle::media::path_utf8(std::filesystem::path(wide[i])));
    std::vector<char *> argv;
    for (auto &arg : storage)
        argv.push_back(arg.data());
    return run(argc, argv.data());
}
#else
int main(int argc, char **argv) { return run(argc, argv); }
#endif
