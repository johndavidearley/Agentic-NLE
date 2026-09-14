#include "commands/editor.hpp"
#include "project/persistence.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <type_traits>

namespace {
using namespace nle;
std::size_t checks = 0;
void check(bool result, const char *expression, int line) {
    ++checks;
    if (!result)
        throw std::runtime_error(std::string(expression) + " at line " + std::to_string(line));
}
#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)
template <typename Fn> void rejects(Fn action) {
    ++checks;
    try {
        action();
    } catch (const DomainError &) {
        return;
    }
    throw std::runtime_error("expected DomainError");
}
bool same_content(ProjectSnapshot a, ProjectSnapshot b) {
    a.revision = b.revision;
    a.operations = b.operations;
    a.next_id = b.next_id; // Allocation watermark is intentionally never rewound.
    return a == b;
}
struct Fixture {
    Editor editor{"Test"};
    SequenceId sequence = *editor.execute(CreateSequence{"Main", {1001, 24000}}).sequence;
    TrackId video = *editor.execute(CreateTrack{sequence, TrackKind::Video, "V1"}).track;
    TrackId audio = *editor.execute(CreateTrack{sequence, TrackKind::Audio, "A1"}).track;
    MediaId media =
        *editor
             .execute(RegisterMedia{
                 "asset", MediaKind::AudioVideo, {60}, {{LocationRole::Original, "source.mov"}}})
             .media;
    ClipId insert(RationalTime position = {0}, RationalTime duration = {10}) {
        return *editor.execute(InsertClip{video, media, position, {{5}, duration}}).clip;
    }
    Clip clip(ClipId id) const {
        for (const auto &seq : editor.snapshot().sequences)
            for (const auto &track : seq.tracks)
                for (const auto &item : track.clips)
                    if (item.id == id)
                        return item;
        throw std::runtime_error("test clip missing");
    }
    void reject(const Command &command) {
        const auto before = editor.snapshot();
        const auto undo = editor.can_undo();
        const auto redo = editor.can_redo();
        rejects([&] { (void)editor.execute(command); });
        CHECK(editor.snapshot() == before);
        CHECK(editor.can_undo() == undo);
        CHECK(editor.can_redo() == redo);
    }
};
void time_tests() {
    CHECK(RationalTime{24, 24} == RationalTime{1});
    CHECK(RationalTime{0, 48000} == RationalTime{});
    CHECK((RationalTime{1001, 24000} + RationalTime{1001, 24000}) == RationalTime{1001, 12000});
    CHECK((RationalTime{1, 48000} + RationalTime{1, 24000}) == RationalTime{1, 16000});
    CHECK((RationalTime{1} - RationalTime{1, 3}) == RationalTime{2, 3});
    rejects([] { (void)RationalTime{-1}; });
    rejects([] { (void)RationalTime{1, 0}; });
    rejects([] { (void)RationalTime{1, -1}; });
    rejects([] { (void)(RationalTime{1} - RationalTime{2}); });
    constexpr auto max = std::numeric_limits<std::int64_t>::max();
    CHECK(RationalTime{max, max - 1} > RationalTime{max - 1, max});
    CHECK(RationalTime{max - 2, max - 1} < RationalTime{max - 1, max});
    CHECK(RationalTime{max} > RationalTime{max - 1});
    rejects([] { (void)(RationalTime{max} + RationalTime{1}); });
    rejects([] { (void)(RationalTime{1, max} + RationalTime{1, max - 1}); });
    CHECK((RationalTime{max, 2} - RationalTime{max - 2, 2}) == RationalTime{1});
    // Exhaustive bounded reference arithmetic exercises both continued-fraction branches.
    for (std::int64_t a = 0; a < 16; ++a)
        for (std::int64_t b = 1; b < 16; ++b)
            for (std::int64_t c = 0; c < 16; ++c)
                for (std::int64_t d = 1; d < 16; ++d) {
                    CHECK((RationalTime{a, b} <=> RationalTime{c, d}) == (a * d <=> c * b));
                    CHECK((RationalTime{a, b} + RationalTime{c, d}) ==
                          RationalTime{a * d + c * b, b * d});
                    if (a * d >= c * b)
                        CHECK((RationalTime{a, b} - RationalTime{c, d}) ==
                              RationalTime{a * d - c * b, b * d});
                }
}
void creation_tests() {
    static_assert(!std::is_convertible_v<TrackId, ClipId>);
    static_assert(!std::is_convertible_v<std::uint64_t, ClipId>);
    Fixture f;
    auto snapshot = f.editor.snapshot();
    CHECK(snapshot.id.value != 0);
    CHECK(snapshot.sequences[0].id == f.sequence);
    CHECK(snapshot.sequences[0].tracks[0].id == f.video);
    CHECK(snapshot.sequences[0].tracks[1].id == f.audio);
    CHECK(snapshot.media[0].id == f.media);
    CHECK(snapshot.media[0].locations[0].uri == "source.mov");
    snapshot.name = "mutated detached snapshot";
    snapshot.sequences.clear();
    CHECK(f.editor.snapshot().name == "Test");
    CHECK(f.editor.snapshot().sequences.size() == 1);
    f.reject(CreateSequence{"", {1, 24}});
    f.reject(CreateSequence{"bad", {0}});
    f.reject(CreateTrack{SequenceId{999}, TrackKind::Video, "bad"});
    f.reject(CreateTrack{f.sequence, static_cast<TrackKind>(99), "bad"});
    f.reject(RegisterMedia{"bad", MediaKind::Audio, {0}, {}});
    f.reject(RegisterMedia{
        "bad", MediaKind::Audio, {1}, {{LocationRole::Proxy, "a"}, {LocationRole::Proxy, "b"}}});
    rejects([] { (void)Editor{""}; });
}
void insert_tests() {
    Fixture f;
    const auto later = f.insert({20});
    const auto first = f.insert({0});
    const auto middle = f.insert({10});
    const auto track = f.editor.snapshot().sequences[0].tracks[0];
    CHECK(track.clips[0].id == first);
    CHECK(track.clips[1].id == middle);
    CHECK(track.clips[2].id == later);
    f.reject(InsertClip{f.video, f.media, {5}, {{0}, {1}}});
    f.reject(InsertClip{f.video, f.media, {30}, {{60}, {1}}});
    f.reject(InsertClip{f.video, f.media, {30}, {{0}, {0}}});
    f.reject(InsertClip{f.video, MediaId{999}, {30}, {{0}, {1}}});
    f.reject(InsertClip{TrackId{999}, f.media, {30}, {{0}, {1}}});
    const auto audio_only =
        *f.editor.execute(RegisterMedia{"audio", MediaKind::Audio, {60}, {}}).media;
    f.reject(InsertClip{f.video, audio_only, {30}, {{0}, {1}}});
    (void)f.editor.execute(InsertClip{f.audio, audio_only, {0}, {{0}, {1, 48000}}});
    CHECK(f.editor.snapshot().sequences[0].tracks[1].clips.size() == 1);
    constexpr auto max = std::numeric_limits<std::int64_t>::max();
    f.reject(InsertClip{f.video, f.media, {max}, {{0}, {1}}});
}
void move_tests() {
    Fixture f;
    const auto id = f.insert();
    const auto original = f.clip(id);
    (void)f.editor.execute(MoveClip{id, f.video, {15}});
    CHECK(f.clip(id).id == id);
    CHECK(f.clip(id).position == RationalTime{15});
    CHECK(f.clip(id).source == original.source);
    (void)f.editor.execute(MoveClip{id, f.audio, {2}});
    CHECK(f.editor.snapshot().sequences[0].tracks[0].clips.empty());
    CHECK(f.editor.snapshot().sequences[0].tracks[1].clips[0].id == id);
    (void)f.editor.execute(InsertClip{f.audio, f.media, {20}, {{0}, {5}}});
    f.reject(MoveClip{id, f.audio, {19}});
    f.reject(MoveClip{id, TrackId{999}, {0}});
    f.reject(MoveClip{ClipId{999}, f.audio, {0}});
    const auto video_only =
        *f.editor.execute(RegisterMedia{"video", MediaKind::Video, {20}, {}}).media;
    const auto video_clip =
        *f.editor.execute(InsertClip{f.video, video_only, {0}, {{0}, {1}}}).clip;
    f.reject(MoveClip{video_clip, f.audio, {0}});
    const auto second_sequence = *f.editor.execute(CreateSequence{"Second"}).sequence;
    const auto second_track =
        *f.editor.execute(CreateTrack{second_sequence, TrackKind::Video, "V1"}).track;
    (void)f.editor.execute(MoveClip{video_clip, second_track, {1}});
    CHECK(f.editor.snapshot().sequences[1].tracks[0].clips[0].id == video_clip);
}
void trim_tests() {
    Fixture f;
    const auto id = f.insert();
    (void)f.editor.execute(TrimClip{id, {2}, {{7}, {6}}});
    CHECK(f.clip(id).source == TimeRange{{7}, {6}});
    CHECK(f.clip(id).position == RationalTime{2});
    CHECK(f.clip(id).id == id);
    (void)f.editor.execute(TrimClip{id, {0}, {{0}, {60}}});
    CHECK(f.clip(id).source.end() == RationalTime{60});
    f.reject(TrimClip{id, {0}, {{0}, {0}}});
    f.reject(TrimClip{id, {0}, {{59}, {2}}});
    f.reject(TrimClip{ClipId{999}, {0}, {{0}, {1}}});
    (void)f.editor.execute(TrimClip{id, {0}, {{0}, {5}}});
    (void)f.insert({10}, {5});
    f.reject(TrimClip{id, {0}, {{0}, {11}}});
    f.reject(TrimClip{id, {8}, {{0}, {5}}});
}
void split_tests() {
    Fixture f;
    const auto id = f.insert({2}, {10});
    const auto original = f.clip(id);
    f.reject(SplitClip{id, {2}});
    f.reject(SplitClip{id, {12}});
    f.reject(SplitClip{id, {1}});
    f.reject(SplitClip{ClipId{999}, {5}});
    const auto right = *f.editor.execute(SplitClip{id, {11, 2}}).clip;
    const auto left_clip = f.clip(id);
    const auto right_clip = f.clip(right);
    CHECK(left_clip.id == id);
    CHECK(right != id);
    CHECK(right_clip.media == left_clip.media);
    CHECK(left_clip.source.duration + right_clip.source.duration == original.source.duration);
    CHECK(left_clip.source.end() == right_clip.source.start);
    CHECK(right_clip.source.end() == original.source.end());
    CHECK(left_clip.position + left_clip.source.duration == right_clip.position);
    const auto split_state = f.editor.snapshot();
    CHECK(f.editor.undo());
    CHECK(f.clip(id) == original);
    CHECK(f.editor.redo());
    CHECK(same_content(f.editor.snapshot(), split_state));
}
void delete_tests() {
    Fixture f;
    const auto id = f.insert();
    const auto other = f.insert({20});
    const auto before = f.editor.snapshot();
    (void)f.editor.execute(DeleteClip{id});
    CHECK(f.editor.snapshot().sequences[0].tracks[0].clips.size() == 1);
    CHECK(f.clip(other).position == RationalTime{20}); // No implicit ripple.
    CHECK(f.editor.undo());
    CHECK(same_content(f.editor.snapshot(), before));
    CHECK(f.editor.redo());
    f.reject(DeleteClip{id});
}
void history_tests() {
    Editor editor("History");
    CHECK(!editor.undo());
    CHECK(!editor.redo());
    std::vector<ProjectSnapshot> states{editor.snapshot()};
    const auto run = [&](const Command &command) {
        auto result = editor.execute(command);
        states.push_back(editor.snapshot());
        return result;
    };
    const auto sequence = *run(CreateSequence{"Main"}).sequence;
    const auto track = *run(CreateTrack{sequence, TrackKind::Video, "V1"}).track;
    const auto media = *run(RegisterMedia{"asset", MediaKind::Video, {60}, {}}).media;
    const auto clip = *run(InsertClip{track, media, {0}, {{2}, {10}}}).clip;
    (void)run(MoveClip{clip, track, {2}});
    (void)run(TrimClip{clip, {2}, {{3}, {8}}});
    const auto right = *run(SplitClip{clip, {6}}).clip;
    (void)run(DeleteClip{right});
    for (std::size_t i = states.size() - 1; i > 0; --i) {
        CHECK(editor.undo());
        CHECK(same_content(editor.snapshot(), states[i - 1]));
    }
    CHECK(!editor.undo());
    for (std::size_t i = 1; i < states.size(); ++i) {
        CHECK(editor.redo());
        CHECK(same_content(editor.snapshot(), states[i]));
    }
    CHECK(!editor.redo());
    CHECK(editor.undo());
    (void)editor.execute(MoveClip{clip, track, {2}}); // No-op must not clear redo.
    CHECK(editor.can_redo());
    const auto before = editor.snapshot();
    rejects([&] { (void)editor.execute(DeleteClip{ClipId{999}}); });
    CHECK(editor.snapshot() == before);
    CHECK(editor.can_redo());
    CHECK(editor.undo()); // Undo split, but retain its allocation watermark.
    const auto new_right = *editor.execute(SplitClip{clip, {5}}).clip;
    CHECK(new_right.value > right.value);
    CHECK(!editor.can_redo());
}
void persistence_tests() {
    Fixture f;
    const auto id = f.insert();
    const auto right = *f.editor.execute(SplitClip{id, {4}}).clip;
    CHECK(f.editor.undo());
    const auto snapshot = f.editor.snapshot();
    const auto data = serialize(snapshot);
    const auto restored = deserialize(data);
    CHECK(restored == snapshot);
    CHECK(serialize(restored) == data);
    Editor loaded(restored);
    CHECK(!loaded.can_undo());
    CHECK(!loaded.can_redo());
    const auto next = *loaded.execute(SplitClip{id, {4}}).clip;
    CHECK(next.value > right.value);
    auto escaped = snapshot;
    escaped.name = "Quotes \" and \\ and UTF-8: caf\xC3\xA9";
    escaped.media[0].locations.push_back({LocationRole::Proxy, "D:\\media\\proxy.mov"});
    CHECK(deserialize(serialize(escaped)) == escaped);
    rejects([&] { (void)deserialize(data + "garbage"); });
    rejects([] { (void)deserialize("NLE_PROJECT 99"); });
    rejects([] { (void)deserialize(""); });
    const auto replace_once = [&](std::string from, std::string to) {
        auto invalid = data;
        const auto position = invalid.find(from);
        CHECK(position != std::string::npos);
        invalid.replace(position, from.size(), to);
        rejects([&] { (void)deserialize(invalid); });
    };
    replace_once("MEDIA 1", "MEDIA -1");
    replace_once("MEDIA 1", "MEDIA 100001");
    replace_once("MEDIA 1", "MEDIA 18446744073709551616");
    replace_once("NLE_PROJECT 4", "NLE_PROJECT 4.0");
    replace_once("\"Test\"", "Test");
    replace_once("ASSET 4 2", "ASSET 4 99");
    replace_once("TRACK 2 0", "TRACK 2 99");
    replace_once("LOCATION 0", "LOCATION 99");
    replace_once("60 1", "60 0");
    replace_once("60 1", "-60 1");
    replace_once("CLIP 5 4", "CLIP 5 999");
    for (std::size_t i = 0; i < data.find("END") + 3; ++i)
        rejects([&] { (void)deserialize(std::string_view(data).substr(0, i)); });
    rejects([] { (void)deserialize(std::string(16 * 1024 * 1024 + 1, 'x')); });
    const auto path = std::filesystem::path("persistence-test.nle");
    save_project(snapshot, path);
    CHECK(load_project(path) == snapshot);
    save_project(escaped, path); // Replacing an existing project works on Windows too.
    CHECK(load_project(path) == escaped);
    auto invalid = escaped;
    invalid.name.clear();
    rejects([&] { save_project(invalid, path); });
    CHECK(load_project(path) == escaped); // Failed validation preserves existing bytes.
    CHECK(std::filesystem::remove(path));
    rejects([&] { (void)load_project(path); });
    const auto unicode_path = std::filesystem::path(u8"persistence-caf\u00e9.nle");
    save_project(snapshot, unicode_path);
    CHECK(load_project(unicode_path) == snapshot);
    CHECK(std::filesystem::remove(unicode_path));
    const auto directory_target = std::filesystem::path("save-failure-test");
    CHECK(std::filesystem::create_directory(directory_target));
    bool failed = false;
    try {
        save_project(snapshot, directory_target);
    } catch (const std::exception &) {
        failed = true;
    }
    CHECK(failed);
    CHECK(std::filesystem::is_directory(directory_target));
    CHECK(std::filesystem::is_empty(directory_target));
    CHECK(std::filesystem::remove(directory_target));
    for (const auto &entry : std::filesystem::directory_iterator("."))
        CHECK(entry.path().filename().string().find("save-failure-test.tmp-") != 0);
}
void validation_tests() {
    Fixture f;
    const auto id = f.insert();
    (void)f.insert({20});
    const auto snapshot = f.editor.snapshot();
    const auto invalid = [&](auto change) {
        auto copy = snapshot;
        change(copy);
        rejects([&] { (void)Editor(copy); });
        rejects([&] { (void)serialize(copy); });
    };
    invalid([](auto &p) { p.id.value = 0; });
    invalid([](auto &p) { p.next_id = 1; });
    invalid([](auto &p) { p.media[0].id.value = 0; });
    invalid([](auto &p) { p.sequences[0].id.value = p.media[0].id.value; });
    invalid([](auto &p) {
        p.sequences[0].tracks[0].clips[1].id = p.sequences[0].tracks[0].clips[0].id;
    });
    invalid([](auto &p) { p.sequences[0].tracks[0].clips[0].media.value = 999; });
    invalid([](auto &p) { p.sequences[0].tracks[0].clips[0].source.duration = RationalTime{}; });
    invalid([](auto &p) { p.sequences[0].tracks[0].clips[0].source.start = RationalTime{60}; });
    invalid([](auto &p) { p.sequences[0].tracks[0].clips[1].position = RationalTime{5}; });
    invalid([](auto &p) {
        std::reverse(p.sequences[0].tracks[0].clips.begin(), p.sequences[0].tracks[0].clips.end());
    });
    invalid([](auto &p) { p.name = "bad\nname"; });
    invalid([](auto &p) { p.name.assign(4097, 'a'); });
    invalid([](auto &p) { p.media[0].locations[0].uri.clear(); });
    auto exhausted = snapshot;
    exhausted.next_id = std::numeric_limits<std::uint64_t>::max();
    Editor editor(exhausted);
    rejects([&] { (void)editor.execute(CreateSequence{"No ID left"}); });
    CHECK(editor.snapshot() == exhausted);
    CHECK(f.clip(id).id == id);
}
} // namespace
int main(int argc, char **argv) {
    const std::map<std::string, std::function<void()>> suites{{"time", time_tests},
                                                              {"creation", creation_tests},
                                                              {"insert", insert_tests},
                                                              {"move", move_tests},
                                                              {"trim", trim_tests},
                                                              {"split", split_tests},
                                                              {"delete", delete_tests},
                                                              {"history", history_tests},
                                                              {"persistence", persistence_tests},
                                                              {"validation", validation_tests}};
    try {
        if (argc != 2 || !suites.contains(argv[1]))
            throw std::runtime_error("expected a valid suite name");
        suites.at(argv[1])();
        std::cout << argv[1] << ": " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
