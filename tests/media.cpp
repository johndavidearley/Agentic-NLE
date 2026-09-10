#include "media/probe.hpp"
#include "project/persistence.hpp"
#include <atomic>
#include <fstream>
#include <iostream>
#include <thread>
using namespace nle;
using namespace nle::media;
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition))                                                                          \
            throw std::runtime_error("check failed: " #condition);                                 \
    } while (false)
struct JoinGuard {
    explicit JoinGuard(std::thread &thread) : thread(thread) {}
    ~JoinGuard() {
        if (thread.joinable())
            thread.join();
    }
    JoinGuard(const JoinGuard &) = delete;
    JoinGuard &operator=(const JoinGuard &) = delete;
    std::thread &thread;
};
template <typename F> void rejects(F fn) {
    bool failed = false;
    try {
        fn();
    } catch (const DomainError &) {
        failed = true;
    }
    CHECK(failed);
}
const std::string fixture =
    "[PROGRAM_VERSION]\nversion=test\nconfiguration=--test\n[/PROGRAM_VERSION]\n"
    "[STREAM]\nindex=0\ncodec_name=pcm_s16le\ncodec_type=audio\nsample_rate=48000\nchannels=2\n"
    "time_base=1/48000\nduration_ts=96000\nstart_pts=-1024\n[/STREAM]\n"
    "[FORMAT]\nformat_name=wav\nduration=2.000000\n[/FORMAT]\n";
std::string replace(std::string value, const std::string &old, const std::string &next) {
    const auto at = value.find(old);
    CHECK(at != std::string::npos);
    value.replace(at, old.size(), next);
    return value;
}
void parsing() {
    const auto source = parse_probe(fixture, 384044);
    CHECK(source_duration(source) == RationalTime{2});
    CHECK(source_kind(source) == MediaKind::Audio);
    CHECK(source.streams[0].duration_ticks == 96000);
    CHECK(source.streams[0].start_ticks == -1024);
    auto fallback = parse_probe(replace(fixture, "duration_ts=96000", "duration_ts=N/A"), 1);
    CHECK(fallback.streams[0].duration_ticks == -1);
    CHECK(source_duration(fallback) == RationalTime{2});
    rejects([] { (void)parse_probe(replace(fixture, "duration_ts=96000", "duration_ts=12x"), 1); });
    rejects([] { (void)parse_probe(replace(fixture, "index=0", "index=0\nindex=1"), 1); });
    rejects([] { (void)parse_probe(replace(fixture, "time_base=1/48000", "time_base=1/0"), 1); });
    rejects([] { (void)parse_probe(fixture.substr(0, fixture.size() - 10), 1); });
    rejects(
        [] { (void)parse_probe(replace(fixture, "codec_type=audio", "codec_type=subtitle"), 1); });
    rejects([] {
        (void)parse_probe(replace(fixture, "duration_ts=96000", "duration_ts=9223372036854775808"),
                          1);
    });
    rejects([] { (void)parse_probe(replace(fixture, "duration_ts=96000", "duration_ts=0"), 1); });
    rejects([] { (void)parse_probe(std::string(1024 * 1024 + 1, 'x'), 1); });
    rejects([] {
        (void)parse_probe(replace(replace(fixture, "duration_ts=96000", "duration_ts=N/A"),
                                  "duration=2.000000", "duration=N/A"),
                          1);
    });
    auto video = replace(fixture, "codec_type=audio\nsample_rate=48000\nchannels=2",
                         "codec_type=video\nwidth=1920\nheight=1080\navg_frame_rate=30000/"
                         "1001\nr_frame_rate=30000/1001");
    CHECK((parse_probe(video, 1).streams[0].frame_duration == RationalTime{1001, 30000}));
    rejects([&] {
        (void)parse_probe(replace(video, "index=0", "index=0\nDISPOSITION:attached_pic=1"), 1);
    });
    auto overflow = source.streams[0];
    overflow.time_base = {3};
    overflow.duration_ticks = 9223372036854775807LL;
    rejects([&] { (void)stream_duration(overflow, {}); });
}
void commands() {
    Editor editor("metadata");
    ProbeResult result{"original.wav", parse_probe(fixture, 100)};
    const auto media = *editor.execute(result.import_command()).media;
    const auto sequence = *editor.execute(CreateSequence{"main"}).sequence;
    const auto track = *editor.execute(CreateTrack{sequence, TrackKind::Audio, "A1"}).track;
    const auto clip = *editor.execute(InsertClip{track, media, {}, {{}, {2}}}).clip;
    const auto before = editor.snapshot();
    auto shorter = result;
    shorter.uri = "short.wav";
    shorter.source.streams[0].duration_ticks = 48000;
    rejects([&] { (void)editor.execute(shorter.relink_command(media)); });
    CHECK(editor.snapshot() == before);
    result.uri = "replacement.wav";
    (void)editor.execute(result.relink_command(media));
    CHECK(editor.snapshot().media[0].id == media);
    CHECK(editor.snapshot().sequences[0].tracks[0].clips[0].id == clip);
    CHECK(editor.undo());
    CHECK(editor.snapshot().media == before.media);
    CHECK(editor.redo());
    CHECK(editor.snapshot().media[0].locations[0].uri == result.uri);
    CHECK(deserialize(serialize(editor.snapshot())) == editor.snapshot());
    const auto current = editor.snapshot();
    rejects([&] { (void)editor.execute(result.relink_command(media), {{}, "stale", 0}); });
    CHECK(editor.snapshot() == current);
    (void)editor.execute(RelinkMedia{media, LocationRole::Proxy, "proxy.wav"});
    CHECK(editor.snapshot().media[0].source.has_value());
    (void)editor.execute(RelinkMedia{media, LocationRole::Original, "unverified.wav"});
    CHECK(!editor.snapshot().media[0].source);
    CHECK(editor.undo());
    CHECK(editor.snapshot().media[0].source.has_value());
    auto invalid = editor.snapshot();
    invalid.media[0].duration = {3};
    rejects([&] { validate(invalid); });
    invalid = editor.snapshot();
    invalid.media[0].source->streams.push_back(invalid.media[0].source->streams[0]);
    rejects([&] { validate(invalid); });
    const auto encoded = serialize(editor.snapshot());
    rejects([&] { (void)deserialize(replace(encoded, "SOURCE 1", "SOURCE 2")); });
    const auto legacy = deserialize("NLE_PROJECT 2\nPROJECT 42 2 \"old\"\nMEDIA 1\nASSET 1 1 \"a\" "
                                    "2 1 0\nSEQUENCES 0\nAUDIT 0 0\nEND\n");
    CHECK(!legacy.media[0].source);
    CHECK(legacy.media[0].id == MediaId{1});
    CHECK(deserialize(serialize(legacy)) == legacy);
}
void process(const std::filesystem::path &helper) {
    const std::vector<std::string> args{
        "echo", "", "with spaces", "quote\"and\\", "caf\xc3\xa9 & literal", "$(echo nope)"};
    CHECK(run_process(helper, args) ==
          "\nwith spaces\nquote\"and\\\ncaf\xc3\xa9 & literal\n$(echo nope)\n");
    for (int i = 0; i < 20; ++i)
        CHECK(run_process(helper, {"echo", "last output"}) == "last output\n");
    rejects([&] { (void)run_process(helper, {"fail"}); });
    rejects([&] { (void)run_process(helper, {"sleep"}, {std::chrono::milliseconds(50)}); });
    rejects([&] { (void)run_process(helper, {"flood"}, {std::chrono::seconds(5), 128}); });
    rejects([&] { (void)run_process(helper.parent_path() / "missing-executable-123", {}); });
    std::atomic_bool stop = false;
    std::thread cancel([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        stop.store(true, std::memory_order_relaxed);
    });
    JoinGuard join_cancel(cancel);
    rejects([&] {
        (void)run_process(helper, {"sleep"}, {std::chrono::seconds(5), 1024, std::cref(stop)});
    });
    rejects([&] {
        (void)run_process(helper, {"echo"}, {std::chrono::seconds(5), 1024, std::cref(stop)});
    });
}
void integration(const std::filesystem::path &executable, const std::filesystem::path &directory) {
    const auto audio = probe(directory / "tone.wav", executable);
    CHECK(source_kind(audio.source) == MediaKind::Audio);
    CHECK(source_duration(audio.source) == RationalTime{2});
    CHECK(audio.source.streams[0].sample_rate == 48000);
    CHECK(audio.source.streams[0].duration_ticks == 96000);
    const auto video = probe(directory / "video.mp4", executable);
    CHECK(source_kind(video.source) == MediaKind::Video);
    CHECK((video.source.streams[0].frame_duration == RationalTime{1001, 30000}));
    const auto av = probe(directory / "av.mkv", executable);
    CHECK(source_kind(av.source) == MediaKind::AudioVideo);
    CHECK(av.source.streams.size() == 2);
    CHECK(source_duration(av.source) > RationalTime{});
    CHECK(probe(directory / utf8_path("caf\xc3\xa9 & tone.wav"), executable).source ==
          audio.source);
    rejects([&] { (void)probe(directory / "missing.wav", executable); });
    rejects([&] { (void)probe(directory / "corrupt.wav", executable); });
    rejects([&] { (void)probe(directory / "network.m3u8", executable); });
    rejects([&] { (void)probe(directory, executable); });
    Editor editor("real corpus");
    const auto id = *editor.execute(audio.import_command()).media;
    CHECK(source_status(editor.snapshot().media[0]) == SourceStatus::Available);
    const auto sequence = *editor.execute(CreateSequence{"main"}).sequence;
    const auto track = *editor.execute(CreateTrack{sequence, TrackKind::Audio, "A1"}).track;
    (void)editor.execute(InsertClip{track, id, {}, {{}, {2}}});
    const auto before = editor.snapshot();
    const auto shorter = probe(directory / "short.wav", executable);
    rejects([&] { (void)editor.execute(shorter.relink_command(id)); });
    rejects([&] { (void)editor.execute(video.relink_command(id)); });
    CHECK(editor.snapshot() == before);
    const auto copy = directory / "offline.wav";
    std::filesystem::copy_file(directory / "tone.wav", copy,
                               std::filesystem::copy_options::overwrite_existing);
    const auto replacement = probe(copy, executable);
    (void)editor.execute(replacement.relink_command(id));
    std::filesystem::remove(copy);
    CHECK(source_status(editor.snapshot().media[0]) == SourceStatus::Missing);
    CHECK(editor.undo());
    CHECK(source_status(editor.snapshot().media[0]) == SourceStatus::Available);
    auto asset = editor.snapshot().media[0];
    asset.source->byte_size += 1;
    CHECK(source_status(asset) == SourceStatus::SizeChanged);
    asset.locations.clear();
    CHECK(source_status(asset) == SourceStatus::Unlocated);
    CHECK(deserialize(serialize(editor.snapshot())) == editor.snapshot());
    std::cout << "Validated real WAV, fractional-rate MP4, AV Matroska, Unicode, "
                 "corrupt/offline/relink corpus using "
              << audio.source.probe_version << '\n';
}
int main(int argc, char **argv) {
    try {
        if (argc < 2)
            throw std::runtime_error("missing suite");
        const std::string suite = argv[1];
        if (suite == "parsing")
            parsing();
        else if (suite == "commands")
            commands();
        else if (suite == "process" && argc == 3)
            process(utf8_path(argv[2]));
        else if (suite == "integration" && argc == 4)
            integration(utf8_path(argv[2]), utf8_path(argv[3]));
        else
            throw std::runtime_error("unknown suite");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
