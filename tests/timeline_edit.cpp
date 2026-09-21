#include "commands/timeline.hpp"
#include <iostream>
using namespace nle;
#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__))                                                                        \
            throw std::runtime_error("check failed: " #__VA_ARGS__);                               \
    } while (false)
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const DomainError &) {
        return;
    }
    throw std::runtime_error("expected rejection");
}
int main() {
    try {
        const RationalTime frame{1001, 30000};
        CHECK(snap_time({10009, 10000}, frame, {50}, {}, {1, 100}) == RationalTime(1001, 1000));
        CHECK(snap_time({10009, 10000}, frame, {50}, {}, {1, 100000}) ==
              RationalTime(10009, 10000));
        const std::vector<RationalTime> edges{{1, 2}, {17, 10}};
        CHECK(snap_time({5001, 10000}, frame, {50}, edges, {1, 100}) == RationalTime(1, 2));
        CHECK(snap_time({2}, frame, {2}, edges, {1, 100}) == RationalTime(2));
        Editor e("Gesture commands");
        const auto s = *e.execute(CreateSequence{"Main", frame}).sequence;
        const auto t = *e.execute(CreateTrack{s, TrackKind::Video, "V1"}).track;
        const auto other = *e.execute(CreateTrack{s, TrackKind::Video, "V2"}).track;
        const auto a = *e.execute(CreateTrack{s, TrackKind::Audio, "A1"}).track;
        const auto m = *e.execute(RegisterMedia{"Source", MediaKind::Video, {20}, {}}).media;
        const auto c = *e.execute(InsertClip{t, m, {5}, {{2}, {3}}}).clip;
        e.execute(InsertClip{t, m, {10}, {{0}, {2}}});
        const auto original = e.snapshot();
        auto move = timeline_drag(original, s, c, other, ClipGesture::Move, {4}, false, {}, {});
        e.execute(move);
        CHECK(e.snapshot().sequences[0].tracks[1].clips[0].source == TimeRange({2}, {3}));
        CHECK(e.undo() && e.snapshot().sequences == original.sequences);
        CHECK(e.redo() && e.undo());
        e.execute(timeline_drag(e.snapshot(), s, c, t, ClipGesture::TrimLeft, {6}, false, {}, {}));
        CHECK(e.snapshot().sequences[0].tracks[0].clips[0].source == TimeRange({3}, {2}));
        CHECK(e.undo());
        e.execute(timeline_drag(e.snapshot(), s, c, t, ClipGesture::TrimLeft, {4}, false, {}, {}));
        CHECK(e.snapshot().sequences[0].tracks[0].clips[0].source == TimeRange({1}, {4}));
        CHECK(e.undo());
        e.execute(timeline_drag(e.snapshot(), s, c, t, ClipGesture::TrimRight, {9}, false, {}, {}));
        CHECK(e.snapshot().sequences[0].tracks[0].clips[0].source == TimeRange({2}, {4}));
        CHECK(e.undo());
        const auto before = e.snapshot();
        rejects([&] {
            e.execute(timeline_drag(before, s, c, t, ClipGesture::Move, {9}, false, {}, {}));
        });
        rejects([&] {
            e.execute(timeline_drag(before, s, c, a, ClipGesture::Move, {0}, false, {}, {}));
        });
        rejects([&] {
            e.execute(timeline_drag(before, s, c, t, ClipGesture::TrimRight, {30}, false, {}, {}));
        });
        rejects([&] { timeline_drag(before, s, c, t, ClipGesture::TrimLeft, {2}, false, {}, {}); });
        rejects([&] { timeline_drag(before, s, c, t, ClipGesture::TrimLeft, {8}, false, {}, {}); });
        rejects(
            [&] { timeline_drag(before, s, c, other, ClipGesture::TrimLeft, {6}, false, {}, {}); });
        CHECK(e.snapshot() == before);
        // Only the moving clip's right edge is close enough to the neighbour.
        const auto endSnap = std::get<MoveClip>(timeline_drag(
            before, s, c, t, ClipGesture::Move, {69999, 10000}, true, {50}, {1, 5000}));
        CHECK(endSnap.position == RationalTime{7});
        // A rejected expected revision cannot replace the live state.
        auto provisional = e.begin();
        provisional.execute(move);
        CHECK(e.snapshot() == before);
        e.execute(SetTrackPlayback{t, {true, true, 1000}});
        const auto newer = e.snapshot();
        rejects([&] { e.commit(std::move(provisional)); });
        CHECK(e.snapshot() == newer);
        std::cout << "Exact snapping, trim source boundaries, overlap, detached preview and undo "
                     "passed\n";
        return 0;
    } catch (const std::exception &x) {
        std::cerr << x.what() << '\n';
        return 1;
    }
}
