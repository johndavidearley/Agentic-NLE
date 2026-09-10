#include "commands/editor.hpp"
#include "project/persistence.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>

namespace {
using namespace nle;
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
ProjectSnapshot fixture(std::size_t clips) {
    ProjectSnapshot state{ProjectId{1}, "Benchmark", static_cast<std::uint64_t>(clips + 4), {}, {}};
    state.media.push_back({MediaId{1}, "Offline media", MediaKind::Video, {10}, {}});
    state.sequences.push_back({SequenceId{2}, "Main", {1, 24}, {}});
    state.sequences[0].tracks.push_back({TrackId{3}, "V1", TrackKind::Video, {}});
    for (std::size_t i = 0; i < clips; ++i)
        state.sequences[0].tracks[0].clips.push_back({ClipId{static_cast<std::uint64_t>(i + 4)},
                                                      MediaId{1},
                                                      {static_cast<std::int64_t>(i * 2)},
                                                      {{0}, {1}}});
    return state;
}
void run(std::size_t count) {
    Editor editor(fixture(count));
    auto start = Clock::now();
    auto batch = editor.begin({{ActorId{"benchmark"}, ActorKind::System}, "100 moves", {}});
    for (std::uint64_t i = 0; i < 100; ++i)
        (void)batch.execute(
            MoveClip{ClipId{i + 4}, TrackId{3}, {static_cast<std::int64_t>(i * 4 + 1), 2}});
    const auto preview_ms = milliseconds(start);
    start = Clock::now();
    (void)editor.commit(std::move(batch));
    const auto commit_ms = milliseconds(start);
    start = Clock::now();
    const auto data = serialize(editor.snapshot());
    const auto save_ms = milliseconds(start);
    start = Clock::now();
    const auto loaded = deserialize(data);
    const auto load_ms = milliseconds(start);
    if (loaded != editor.snapshot() || editor.history_usage().undo_entries != 1)
        throw DomainError("benchmark invariant failed");
    std::cout << count << ',' << preview_ms << ',' << commit_ms << ','
              << editor.history_usage().bytes << ',' << save_ms << ',' << load_ms << '\n';
}
} // namespace
int main() {
    try {
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "clips,batch_100_ms,commit_ms,history_bytes,serialize_ms,deserialize_ms\n";
        run(1000);
        run(10000);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}