#include "mcp/project_file.hpp"
#include "mcp/session.hpp"
#include <algorithm>
#include <iostream>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif
namespace {
using namespace nle::mcp;
int run(const std::vector<std::string> &args) {
    try {
        Policy policy;
        std::string project;
        std::vector<std::filesystem::path> media_roots;
        std::string ffprobe;
        std::vector<std::filesystem::path> output_roots;
        bool allow_export = false;
        bool actor_seen = false, recovery = false, recover = false, discard = false;
        bool allow_media = false;
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--project" && project.empty() && i + 1 < args.size())
                project = args[++i];
            else if (args[i] == "--actor" && !actor_seen && i + 1 < args.size()) {
                policy.actor.id.value = args[++i];
                actor_seen = true;
            } else if (args[i] == "--allow-edit" && !policy.allow_edit)
                policy.allow_edit = true;
            else if (args[i] == "--allow-save" && !policy.allow_save)
                policy.allow_save = true;
            else if (args[i] == "--allow-media" && !allow_media)
                allow_media = true;
            else if (args[i] == "--media-root" && i + 1 < args.size())
                media_roots.push_back(nle::media::utf8_path(args[++i]));
            else if (args[i] == "--ffprobe" && ffprobe.empty() && i + 1 < args.size())
                ffprobe = args[++i];
            else if (args[i] == "--allow-export" && !allow_export)
                allow_export = true;
            else if (args[i] == "--output-root" && i + 1 < args.size())
                output_roots.push_back(nle::media::utf8_path(args[++i]));
            else if (args[i] == "--recovery" && !recovery)
                recovery = true;
            else if (args[i] == "--recover" && !recover && !discard)
                recover = true;
            else if (args[i] == "--discard-recovery" && !discard && !recover)
                discard = true;
            else
                throw Failure("configuration",
                              "Usage: editor-mcp --project FILE [--actor ID] [--allow-edit] "
                              "[--allow-save] [--allow-media --media-root DIR --ffprobe FILE] "
                              "[--allow-export --output-root DIR (export-capable launcher)] "
                              "[--recovery [--recover|--discard-recovery]]");
        }
        if (project.empty())
            throw Failure("configuration", "Select one existing project with --project FILE.");
        std::u8string path;
        for (char c : project)
            path.push_back(static_cast<char8_t>(static_cast<unsigned char>(c)));
        if ((recovery && !policy.allow_edit) || ((recover || discard) && !recovery))
            throw Failure("configuration", "Recovery requires --recovery and --allow-edit; it "
                                           "separately permits checkpoint writes.");
        if ((allow_media && (media_roots.empty() || ffprobe.empty())) ||
            (!allow_media && (!media_roots.empty() || !ffprobe.empty())))
            throw Failure("configuration", "Media access requires --allow-media, at least one "
                                           "--media-root and a fixed --ffprobe executable.");
        if (allow_media)
            policy.media_access.emplace(std::move(media_roots), nle::media::utf8_path(ffprobe));
        if (allow_export != !output_roots.empty() || (allow_export && !allow_media))
            throw Failure("configuration",
                          "Export requires --allow-export, --output-root and media permission.");
        if (allow_export) {
#ifdef NLE_MCP_EXPORT
            policy.output_access.emplace(std::move(output_roots), std::filesystem::path(path));
            policy.export_sequence = [access = *policy.media_access](auto snapshot, auto sequence,
                                                                     auto options, auto &stop) {
                auto plan = nle::playback::make_sequence_plan(snapshot, sequence);
                std::vector<std::filesystem::path> sources;
                for (auto &asset : plan.media) {
                    const auto location = std::find_if(
                        asset.locations.begin(), asset.locations.end(),
                        [](const auto &v) { return v.role == nle::LocationRole::Original; });
                    if (location == asset.locations.end())
                        throw Failure("media_unavailable", "Original media is unlocated.");
                    const auto approved =
                        access.approved_file(nle::media::utf8_path(location->uri));
                    location->uri = nle::media::path_utf8(approved);
                    sources.push_back(approved);
                }
                const auto destination_check = options.validate_destination;
                options.validate_destination = [access, sources, destination = options.destination,
                                                destination_check] {
                    destination_check();
                    for (const auto &source : sources) {
                        if (access.approved_file(source) != source)
                            throw Failure("media_path_denied",
                                          "Source path changed during export.");
                        std::error_code error;
                        if (destination == source ||
                            std::filesystem::equivalent(destination, source, error))
                            throw Failure("output_path_denied",
                                          "Export cannot replace a media source.");
                    }
                };
                options.validate_destination();
                return nle::exporting::export_sequence(std::move(plan), options, stop);
            };
#else
            throw Failure(
                "configuration",
                "Use editor-mcp-export for export grants; this server remains lightweight.");
#endif
        }
        ProjectFile file(std::filesystem::path(path), policy.allow_save || recovery);
        auto state = file.load();
        policy.saved_revision = state.revision;
        if (recovery) {
            const auto pending = file.recovery();
            if (discard)
                file.discard_recovery();
            else if (recover) {
                if (!pending.project)
                    throw Failure("recovery_unavailable",
                                  "No matching valid recovery checkpoint is available.");
                state = file.recover();
                if (!pending.warning.empty())
                    std::cerr << pending.warning << '\n';
            } else if (pending.project || !pending.warning.empty())
                throw Failure(
                    "recovery_available",
                    "Recovery files exist. Select --recover or --discard-recovery explicitly.");
        }
        std::function<void(const nle::ProjectSnapshot &)> checkpoint;
        if (recovery)
            checkpoint = [&](const auto &snapshot) { file.checkpoint(snapshot); };
        Session session(
            std::move(state), policy, [&](const auto &snapshot) { file.save(snapshot); },
            Clock::now, checkpoint);
        std::string line;
        char byte{};
        while (std::cin.get(byte)) {
            if (byte == '\n') {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                const auto response = session.receive(line);
                line.clear();
                if (response) {
                    std::cout << response->dump() << '\n' << std::flush;
                    if (!std::cout)
                        return 1;
                }
            } else {
                if (line.size() == max_message_bytes) {
                    std::cout
                        << R"({"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"Message exceeds 1 MiB; closing session."}})"
                        << '\n'
                        << std::flush;
                    return 2;
                }
                line.push_back(byte);
            }
        }
        return std::cin.bad() ? 1 : 0;
    } catch (const nle::FileError &error) {
        std::cerr << error.code << ": " << error.what() << '\n';
    } catch (const Failure &error) {
        std::cerr << error.code << ": " << error.what() << '\n';
    } catch (const std::exception &) {
        std::cerr << "Cannot start the configured MCP project session.\n";
    }
    return 2;
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, nullptr,
                                              0, nullptr, nullptr);
        if (size <= 0)
            return 2;
        std::string value(static_cast<std::size_t>(size), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, value.data(), size,
                                nullptr, nullptr) != size)
            return 2;
        value.pop_back();
        args.push_back(std::move(value));
    }
    return run(args);
}
#else
int main(int argc, char **argv) { return run(std::vector<std::string>(argv, argv + argc)); }
#endif
