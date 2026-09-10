#include "commands/editor.hpp"
#include "project/persistence.hpp"
#include <atomic>
#include <barrier>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
using namespace nle;
std::size_t checks = 0;
void require(bool ok, int line) {
    ++checks;
    if (!ok)
        throw std::runtime_error("check failed at line " + std::to_string(line));
}
#define CHECK(...) require((__VA_ARGS__), __LINE__)
template <typename Error = DomainError, typename Fn> void rejects(Fn fn) {
    ++checks;
    try {
        fn();
    } catch (const Error &) {
        return;
    }
    throw std::runtime_error("expected exception");
}
bool content_equal(ProjectSnapshot a, ProjectSnapshot b) {
    a.revision = b.revision;
    a.operations = b.operations;
    a.next_id = b.next_id;
    return a == b;
}
struct Fixture {
    Editor editor{"Sessions"};
    SequenceId sequence = *editor.execute(CreateSequence{"Main"}).sequence;
    TrackId video = *editor.execute(CreateTrack{sequence, TrackKind::Video, "V1"}).track;
    TrackId audio = *editor.execute(CreateTrack{sequence, TrackKind::Audio, "A1"}).track;
    MediaId media =
        *editor
             .execute(RegisterMedia{
                 "media", MediaKind::AudioVideo, {60}, {{LocationRole::Original, "original.mov"}}})
             .media;
    ClipId clip = *editor.execute(InsertClip{video, media, {0}, {{0}, {10}}}).clip;
};
RequestContext agent(std::uint64_t revision) {
    return {{ActorId{"agent:test"}, ActorKind::Agent}, "Batch edit", revision};
}
void transactions() {
    Fixture f;
    const auto before = f.editor.snapshot();
    auto transaction = f.editor.begin(agent(before.revision));
    (void)transaction.execute(MoveClip{f.clip, f.video, {2}});
    (void)transaction.execute(TrimClip{f.clip, {2}, {{1}, {8}}});
    const auto right = transaction.execute(SplitClip{f.clip, {6}});
    CHECK(right.clip.has_value());
    CHECK(!right.operation.has_value());
    (void)transaction.execute(RelinkMedia{f.media, LocationRole::Proxy, "proxy.mov"});
    auto preview = transaction.preview();
    CHECK(f.editor.snapshot() == before);
    CHECK(preview.revision == before.revision);
    CHECK(preview.operations == before.operations);
    CHECK(preview.sequences[0].tracks[0].clips.size() == 2);
    preview.name = "detached";
    CHECK(transaction.preview().name == before.name);
    const auto proposed = transaction.preview();
    Editor direct(before);
    (void)direct.execute(MoveClip{f.clip, f.video, {2}});
    (void)direct.execute(TrimClip{f.clip, {2}, {{1}, {8}}});
    (void)direct.execute(SplitClip{f.clip, {6}});
    (void)direct.execute(RelinkMedia{f.media, LocationRole::Proxy, "proxy.mov"});
    CHECK(content_equal(direct.snapshot(), proposed));
    const auto operation = f.editor.commit(std::move(transaction));
    CHECK(operation.has_value());
    CHECK(!transaction.active());
    CHECK(f.editor.revision() == before.revision + 1);
    CHECK(content_equal(f.editor.snapshot(), proposed));
    auto records = f.editor.changes_since(before.revision);
    CHECK(records.size() == 1);
    CHECK(records[0].id == *operation);
    CHECK(records[0].actor == agent(0).actor);
    CHECK(records[0].actions.size() == 4);
    CHECK(records[0].actions[2].find("SplitClip") == 0);
    CHECK(records[0].label == "Batch edit");
    CHECK(f.editor.undo(
        {{ActorId{"human:editor"}, ActorKind::Human}, "Undo batch", f.editor.revision()}));
    CHECK(content_equal(f.editor.snapshot(), before));
    CHECK(f.editor.redo());
    CHECK(content_equal(f.editor.snapshot(), proposed));
    records = f.editor.changes_since(before.revision);
    CHECK(records.size() == 3);
    CHECK(records[1].kind == ChangeKind::Undo && records[1].target == operation);
    CHECK(records[2].kind == ChangeKind::Redo && records[2].target == operation);
    CHECK(f.editor.snapshot().sequences[0].tracks[0].clips[1].id == *right.clip);
    rejects([&] { (void)transaction.preview(); });
    rejects([&] { (void)f.editor.commit(std::move(transaction)); });
}
void rollback() {
    Fixture f;
    const auto before = f.editor.snapshot();
    auto transaction = f.editor.begin();
    const auto tentative = transaction.execute(SplitClip{f.clip, {5}}).clip;
    transaction.rollback();
    transaction.rollback();
    CHECK(!transaction.active());
    rejects([&] { (void)f.editor.commit(std::move(transaction)); });
    CHECK(f.editor.snapshot() == before);
    {
        auto discarded = f.editor.begin();
        (void)discarded.execute(DeleteClip{f.clip});
    }
    CHECK(f.editor.snapshot() == before);
    auto failed = f.editor.begin();
    (void)failed.execute(MoveClip{f.clip, f.audio, {2}});
    rejects([&] { (void)failed.execute(TrimClip{f.clip, {2}, {{0}, {0}}}); });
    CHECK(!failed.active());
    rejects([&] { (void)f.editor.commit(std::move(failed)); });
    CHECK(f.editor.snapshot() == before);
    auto moved = f.editor.begin();
    auto owner = std::move(moved);
    CHECK(!moved.active());
    CHECK(owner.active());
    CHECK(!f.editor.commit(std::move(owner))); // Empty batch: no revision/history change.
    CHECK(f.editor.snapshot() == before);
    auto net_zero = f.editor.begin();
    (void)net_zero.execute(MoveClip{f.clip, f.video, {1}});
    (void)net_zero.execute(MoveClip{f.clip, f.video, {0}});
    CHECK(!f.editor.commit(std::move(net_zero)));
    CHECK(f.editor.snapshot() == before);
    const auto committed = f.editor.execute(SplitClip{f.clip, {5}}).clip;
    CHECK(committed == tentative); // Abandoned preview IDs were provisional.
    const auto state = f.editor.snapshot();
    CHECK(f.editor.undo());
    auto no_op = f.editor.begin();
    (void)no_op.execute(MoveClip{f.clip, f.video, {0}});
    CHECK(!f.editor.commit(std::move(no_op)));
    CHECK(f.editor.can_redo());
    CHECK(f.editor.redo());
    CHECK(content_equal(f.editor.snapshot(), state));
    auto too_many = f.editor.begin();
    for (std::size_t i = 0; i < max_batch_commands; ++i)
        (void)too_many.execute(MoveClip{f.clip, f.video, {0}});
    const auto unchanged = f.editor.snapshot();
    rejects([&] { (void)too_many.execute(MoveClip{f.clip, f.video, {0}}); });
    CHECK(!too_many.active());
    CHECK(f.editor.snapshot() == unchanged);
}
void revisions() {
    Fixture f;
    const auto before = f.editor.snapshot();
    auto first = f.editor.begin(agent(before.revision));
    auto stale = f.editor.begin(agent(before.revision));
    (void)first.execute(SplitClip{f.clip, {5}});
    (void)stale.execute(SplitClip{f.clip, {4}});
    CHECK(f.editor.commit(std::move(first)).has_value());
    const auto live = f.editor.snapshot();
    const auto usage = f.editor.history_usage();
    rejects<RevisionConflict>([&] { (void)f.editor.commit(std::move(stale)); });
    CHECK(!stale.active());
    CHECK(f.editor.snapshot() == live);
    CHECK(f.editor.history_usage().bytes == usage.bytes);
    rejects<RevisionConflict>(
        [&] { (void)f.editor.execute(DeleteClip{f.clip}, agent(before.revision)); });
    rejects<RevisionConflict>([&] { (void)f.editor.undo(agent(before.revision)); });
    rejects<RevisionConflict>([&] { (void)f.editor.redo(agent(before.revision)); });
    rejects<RevisionConflict>([&] { (void)f.editor.begin(agent(before.revision)); });
    rejects<RevisionConflict>([&] { (void)f.editor.changes_since(live.revision + 1); });
    CHECK(f.editor.snapshot() == live);
    auto aba = f.editor.begin();
    CHECK(f.editor.undo());
    CHECK(f.editor.redo());
    rejects<RevisionConflict>([&] { (void)f.editor.commit(std::move(aba)); });
    Editor other(f.editor.snapshot());
    auto foreign = f.editor.begin();
    const auto other_state = other.snapshot();
    rejects([&] { (void)other.commit(std::move(foreign)); });
    CHECK(other.snapshot() == other_state);
    const auto invalid_actor = RequestContext{{ActorId{""}, ActorKind::Agent}, "Edit", {}};
    rejects([&] { (void)other.begin(invalid_actor); });
    rejects([&] { (void)other.execute(DeleteClip{f.clip}, invalid_actor); });
    CHECK(other.snapshot() == other_state);
    auto abandoned = [&] {
        Editor temporary("temporary");
        return temporary.begin();
    }();
    CHECK(!abandoned.active());
    rejects([&] { (void)abandoned.preview(); });
}
void concurrency() {
    Fixture f;
    const auto base = f.editor.revision();
    std::barrier start(3);
    std::atomic<int> committed{0}, conflicted{0}, errors{0};
    const auto edit = [&](int position) {
        start.arrive_and_wait();
        try {
            (void)f.editor.execute(MoveClip{f.clip, f.video, {position}}, agent(base));
            ++committed;
        } catch (const RevisionConflict &) {
            ++conflicted;
        } catch (...) {
            ++errors;
        }
    };
    std::thread a(edit, 1);
    std::thread b(edit, 2);
    start.arrive_and_wait();
    a.join();
    b.join();
    CHECK(committed == 1);
    CHECK(conflicted == 1);
    CHECK(errors == 0);
    CHECK(f.editor.revision() == base + 1);
    CHECK(f.editor.changes_since(base).size() == 1);
}
void history_limits() {
    Fixture f;
    const auto initial = f.editor.snapshot();
    Editor limited(initial, {2, 64 * 1024 * 1024});
    for (int i = 1; i <= 3; ++i)
        (void)limited.execute(MoveClip{f.clip, f.video, {i}});
    CHECK(limited.history_usage().undo_entries == 2);
    CHECK(limited.undo());
    CHECK(limited.undo());
    CHECK(!limited.undo());
    CHECK(limited.snapshot().sequences[0].tracks[0].clips[0].position == RationalTime{1});
    CHECK(limited.history_usage().redo_entries == 2);
    const auto retained_bytes = limited.history_usage().bytes;
    CHECK(limited.redo());
    CHECK(limited.history_usage().bytes == retained_bytes);
    (void)limited.execute(MoveClip{f.clip, f.video, {4}});
    CHECK(!limited.can_redo());
    CHECK(limited.history_usage().undo_entries == 2);

    Editor probe(initial);
    (void)probe.execute(MoveClip{f.clip, f.video, {1}});
    const auto single_bytes = probe.history_usage().bytes;
    Editor bytes_limited(initial, {100, single_bytes});
    (void)bytes_limited.execute(MoveClip{f.clip, f.video, {1}});
    (void)bytes_limited.execute(MoveClip{f.clip, f.video, {2}});
    CHECK(bytes_limited.history_usage().undo_entries == 1);
    CHECK(bytes_limited.history_usage().bytes <= single_bytes);
    CHECK(bytes_limited.undo());
    CHECK(!bytes_limited.undo());
    Editor too_small(initial, {100, single_bytes - 1});
    rejects([&] { (void)too_small.execute(MoveClip{f.clip, f.video, {1}}); });
    CHECK(too_small.snapshot() == initial);
    auto batch = too_small.begin();
    (void)batch.execute(MoveClip{f.clip, f.video, {2}});
    rejects([&] { (void)too_small.commit(std::move(batch)); });
    CHECK(!batch.active());
    CHECK(too_small.snapshot() == initial);
    Editor disabled(initial, {0, 0});
    (void)disabled.execute(MoveClip{f.clip, f.video, {1}});
    CHECK(!disabled.can_undo());
    CHECK(disabled.history_usage().bytes == 0);
    CHECK(disabled.changes_since(initial.revision).size() == 1);
}
void track_media() {
    Fixture f;
    auto before = f.editor.snapshot();
    (void)f.editor.execute(ReorderTrack{f.sequence, f.video, 1});
    CHECK(f.editor.snapshot().sequences[0].tracks[1].id == f.video);
    CHECK(f.editor.undo());
    CHECK(content_equal(f.editor.snapshot(), before));
    CHECK(f.editor.redo());
    before = f.editor.snapshot();
    rejects([&] { (void)f.editor.execute(ReorderTrack{f.sequence, f.audio, 2}); });
    rejects([&] { (void)f.editor.execute(ReorderTrack{SequenceId{999}, f.video, 0}); });
    CHECK(f.editor.snapshot() == before);
    (void)f.editor.execute(DeleteTrack{f.video});
    CHECK(f.editor.snapshot().sequences[0].tracks.size() == 1);
    CHECK(f.editor.snapshot().media == before.media);
    CHECK(f.editor.undo());
    CHECK(content_equal(f.editor.snapshot(), before));
    CHECK(f.editor.snapshot().sequences[0].tracks[1].clips[0].id == f.clip);
    (void)f.editor.execute(RelinkMedia{f.media, LocationRole::Original, "moved.mov"});
    CHECK(f.editor.snapshot().media[0].id == f.media);
    CHECK(f.editor.snapshot().media[0].locations[0].uri == "moved.mov");
    CHECK(f.editor.undo());
    CHECK(f.editor.snapshot().media == before.media);
    CHECK(f.editor.redo());
    (void)f.editor.execute(RelinkMedia{f.media, LocationRole::Proxy, "proxy.mov"});
    CHECK(f.editor.snapshot().media[0].locations.size() == 2);
    (void)f.editor.execute(RelinkMedia{f.media, LocationRole::Original, std::nullopt});
    CHECK(f.editor.snapshot().media[0].locations.size() == 1);
    CHECK(f.editor.undo());
    CHECK(f.editor.snapshot().media[0].locations.size() == 2);
    before = f.editor.snapshot();
    rejects([&] { (void)f.editor.execute(RelinkMedia{f.media, LocationRole::Proxy, ""}); });
    rejects([&] { (void)f.editor.execute(RelinkMedia{MediaId{999}, LocationRole::Proxy, "x"}); });
    rejects([&] { (void)f.editor.execute(DeleteTrack{TrackId{999}}); });
    CHECK(f.editor.snapshot() == before);
}
void migration() {
    const std::string v1 = "NLE_PROJECT 1\nPROJECT 42 9 \"Old\"\nMEDIA 1\n"
                           "ASSET 4 2 \"media\" 60 1 1\nLOCATION 0 \"original.mov\"\n"
                           "SEQUENCES 1\nSEQUENCE 1 \"Main\" 1 24 1\n"
                           "TRACK 2 0 \"V1\" 1\nCLIP 5 4 0 1 0 1 10 1\nEND\n";
    const auto migrated = deserialize(v1);
    CHECK(migrated.id == ProjectId{42});
    CHECK(migrated.next_id == 9);
    CHECK(migrated.revision == 0);
    CHECK(migrated.operations.empty());
    CHECK(serialize(migrated).starts_with("NLE_PROJECT 3"));
    Editor editor(migrated);
    auto batch = editor.begin(agent(0));
    const auto right = *batch.execute(SplitClip{ClipId{5}, {5}}).clip;
    CHECK(right.value == 9);
    CHECK(editor.commit(std::move(batch)) == OperationId{1});
    CHECK(editor.undo());
    const auto saved = serialize(editor.snapshot());
    Editor reloaded(deserialize(saved));
    CHECK(reloaded.snapshot() == editor.snapshot());
    CHECK(!reloaded.can_undo() && !reloaded.can_redo());
    CHECK(reloaded.revision() == 2);
    CHECK(reloaded.changes_since(0).size() == 2);
    const auto next = reloaded.execute(SplitClip{ClipId{5}, {5}});
    CHECK(next.clip->value > right.value);
    CHECK(next.operation == OperationId{3});
    CHECK(deserialize(serialize(reloaded.snapshot())) == reloaded.snapshot());
    auto invalid = reloaded.snapshot();
    invalid.operations[1].target = OperationId{2};
    rejects([&] { (void)serialize(invalid); });
    invalid = reloaded.snapshot();
    invalid.operations[0].actor.id.value.clear();
    rejects([&] { (void)Editor(invalid); });
    invalid = reloaded.snapshot();
    invalid.revision = 0;
    rejects([&] { (void)Editor(invalid); });
    rejects([] { (void)deserialize("NLE_PROJECT 4"); });

    auto full = migrated;
    for (std::size_t i = 1; i <= max_operations; ++i)
        full.operations.push_back(
            {OperationId{i}, i, {}, "Imported test edit", ChangeKind::Edit, {}, {"Test"}});
    full.revision = full.operations.size();
    Editor capped(full);
    rejects([&] { (void)capped.execute(DeleteClip{ClipId{5}}); });
    CHECK(capped.snapshot() == full);
}
void fuzz_persistence() {
    Fixture f;
    const auto original = serialize(f.editor.snapshot());
    std::mt19937 random(0xC0DE);
    const std::string alphabet = "0123456789- \"\\\nABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int i = 0; i < 2000; ++i) {
        auto data = original;
        const auto position = static_cast<std::size_t>(random()) % data.size();
        switch (i % 3) {
        case 0:
            data[position] = alphabet[static_cast<std::size_t>(random()) % alphabet.size()];
            break;
        case 1:
            data.erase(position, 1);
            break;
        default:
            data.insert(position, 1, '\0');
            break;
        }
        std::optional<ProjectSnapshot> parsed;
        try {
            parsed = deserialize(data);
        } catch (const DomainError &) {
            ++checks;
        }
        // A reader-accepted document must also survive writer validation.
        if (parsed)
            CHECK(deserialize(serialize(*parsed)) == *parsed);
    }
    CHECK(serialize(f.editor.snapshot()) == original);
}
void save_failure() {
    Fixture f;
    const auto path = std::filesystem::path("session-save.nle");
    const auto old = f.editor.snapshot();
    save_project(old, path);
    (void)f.editor.execute(DeleteClip{f.clip});
#ifdef _WIN32
    struct FileLock {
        HANDLE value;
        ~FileLock() {
            if (value != INVALID_HANDLE_VALUE)
                CloseHandle(value);
        }
    };
    {
        FileLock held{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        CHECK(held.value != INVALID_HANDLE_VALUE);
        rejects([&] { save_project(f.editor.snapshot(), path); });
        CHECK(load_project(path) == old);
    }
#endif
    save_project(f.editor.snapshot(), path);
    CHECK(load_project(path) == f.editor.snapshot());
    CHECK(std::filesystem::remove(path));
    for (const auto &entry : std::filesystem::directory_iterator("."))
        CHECK(entry.path().filename().string().find("session-save.nle.tmp-") != 0);
}
} // namespace
int main(int argc, char **argv) {
    const std::map<std::string, std::function<void()>> suites{
        {"transactions", transactions},     {"rollback", rollback},
        {"revisions", revisions},           {"concurrency", concurrency},
        {"history-limits", history_limits}, {"track-media", track_media},
        {"migration", migration},           {"fuzz-persistence", fuzz_persistence},
        {"save-failure", save_failure}};
    try {
        if (argc != 2 || !suites.contains(argv[1]))
            throw std::runtime_error("unknown suite");
        suites.at(argv[1])();
        std::cout << argv[1] << ": " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}