#include "playback/sequence.hpp"
#include "commands/editor.hpp"
#include "project/persistence.hpp"
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
        Editor e("Layers");
        const auto seq = *e.execute(CreateSequence{"Main", {1001, 30000}}).sequence;
        const auto top = *e.execute(CreateTrack{seq, TrackKind::Video, "B roll"}).track;
        const auto base = *e.execute(CreateTrack{seq, TrackKind::Video, "Dialogue"}).track;
        const auto av = *e.execute(RegisterMedia{"AV", MediaKind::AudioVideo, {30}, {}}).media;
        const auto audio = *e.execute(RegisterMedia{"Music", MediaKind::Audio, {30}, {}}).media;
        const auto cover = *e.execute(InsertClip{top, av, {1001, 30000}, {{1}, {1}}}).clip;
        (void)e.execute(InsertClip{base, av, {}, {{2}, {3}}});
        for (int i = 0; i < 4; ++i) {
            auto track = *e.execute(CreateTrack{seq, TrackKind::Audio, "A"}).track;
            (void)e.execute(InsertClip{track, audio, {}, {{i}, {4}}});
        }
        auto plan = playback::make_sequence_plan(e.snapshot(), seq);
        CHECK(plan.evaluate({}).video->layer == 1);
        const auto active = plan.evaluate({1001, 30000});
        CHECK(active.video->layer == 0 && active.video->source == RationalTime{1});
        CHECK(active.audio.size() == 6); // Both embedded streams, regardless of video coverage.
        CHECK(plan.evaluate(RationalTime{1001, 30000} - RationalTime{1, 48000}).video->layer == 1);
        CHECK(plan.evaluate({4}).audio.empty() && !plan.evaluate({4}).video);
        rejects([&] { (void)plan.evaluate({5}); });
        (void)e.execute(SetTrackPlayback{top, {true, true, 4000}});
        auto muted = playback::make_sequence_plan(e.snapshot(), seq).evaluate({1});
        CHECK(muted.video->layer == 0 && muted.audio.size() == 5);
        (void)e.execute(SetTrackPlayback{top, {false, false, 500}});
        auto disabled = playback::make_sequence_plan(e.snapshot(), seq).evaluate({1});
        CHECK(disabled.video->layer == 1 && disabled.audio.size() == 5);
        CHECK(plan.evaluate({1}).audio.size() == 6); // Detached plan.
        CHECK(e.undo());
        CHECK(e.redo());
        const auto before = e.snapshot();
        rejects([&] { (void)e.execute(SetTrackPlayback{base, {true, false, 4001}}); });
        rejects([&] { (void)e.execute(SetSequenceOutput{seq, {1, 30}, {1919, 1080, 48000, 2}}); });
        rejects([&] { (void)e.execute(SetClipRouting{cover, {{StreamMode::Explicit, 99}, {}}}); });
        CHECK(e.snapshot() == before);
        const ClipRouting route{{StreamMode::Disabled, 0}, {StreamMode::Disabled, 0}};
        (void)e.execute(SetClipRouting{cover, route});
        (void)e.execute(SplitClip{cover, {1, 2}});
        CHECK(e.snapshot().sequences[0].tracks[0].clips[1].routing == route);
        (void)e.execute(SetSequenceOutput{seq, {1001, 30000}, {1280, 720, 48000, 2}});
        const auto text = serialize(e.snapshot());
        CHECK(text.starts_with("NLE_PROJECT 5"));
        CHECK(deserialize(text) == e.snapshot());
        auto legacy = text;
        legacy.replace(0, 13, "NLE_PROJECT 4");
        for (const auto *marker : {"OUTPUT ", "PLAYBACK ", "ROUTING "}) {
            for (auto at = legacy.find(marker); at != std::string::npos; at = legacy.find(marker))
                legacy.erase(at, legacy.find('\n', at) - at + 1);
        }
        const auto migrated = deserialize(legacy);
        CHECK(migrated.sequences[0].output == SequenceOutput{});
        CHECK(migrated.sequences[0].frame_duration == RationalTime(1001, 30000));
        CHECK(migrated.sequences[0].tracks[0].playback == TrackPlayback{});
        CHECK(migrated.sequences[0].tracks[0].clips[0].routing == ClipRouting{});
        CHECK(playback::make_sequence_plan(migrated, seq).evaluate({1}).audio.size() == 6);
        for (const auto &pair : {std::pair{"PLAYBACK 0 0 500", "PLAYBACK 2 0 500"},
                                 std::pair{"ROUTING 1 0 1 0", "ROUTING 3 0 1 0"},
                                 std::pair{"OUTPUT 1280 720 48000 2", "OUTPUT 1280 720 44100 2"}}) {
            auto bad = text;
            const auto at = bad.find(pair.first);
            CHECK(at != std::string::npos);
            bad.replace(at, std::char_traits<char>::length(pair.first), pair.second);
            rejects([&] { (void)deserialize(bad); });
        }
        SourceMetadata metadata{"test", "test", "test", 1, {10}, {}};
        SourceStream v;
        v.codec = "mpeg4";
        v.time_base = {1, 30};
        v.duration_ticks = 300;
        v.width = 64;
        v.height = 48;
        v.start_known = true;
        SourceStream a;
        a.index = 2;
        a.kind = TrackKind::Audio;
        a.codec = "pcm_s16le";
        a.time_base = {1, 48000};
        a.duration_ticks = 480000;
        a.sample_rate = 48000;
        a.channels = 2;
        a.start_known = true;
        metadata.streams = {v, a};
        const auto routed = *e.execute(RegisterMedia{"Routed",
                                                     MediaKind::AudioVideo,
                                                     {10},
                                                     {{LocationRole::Original, "routed.mkv"}},
                                                     metadata})
                                 .media;
        const auto explicitClip =
            *e.execute(InsertClip{base,
                                  routed,
                                  {5},
                                  {{}, {1}},
                                  {{StreamMode::Explicit, 0}, {StreamMode::Explicit, 2}}})
                 .clip;
        const auto explicitPlan = playback::make_sequence_plan(e.snapshot(), seq).evaluate({5});
        CHECK(explicitPlan.video->stream == 0U);
        CHECK(explicitPlan.audio.size() == 1 && explicitPlan.audio[0].stream == 2U);
        const auto audioTrack = e.snapshot().sequences[0].tracks[2].id;
        const auto immutable = e.snapshot();
        rejects([&] { (void)e.execute(MoveClip{explicitClip, audioTrack, {5}}); });
        CHECK(e.snapshot() == immutable);
        (void)e.execute(
            SetClipRouting{explicitClip, {{StreamMode::Disabled, 0}, {StreamMode::Explicit, 2}}});
        (void)e.execute(MoveClip{explicitClip, audioTrack, {5}});
        CHECK(!playback::make_sequence_plan(e.snapshot(), seq).evaluate({5}).video);
        CHECK(e.undo());
        CHECK(e.undo());
        CHECK(e.snapshot().sequences == immutable.sequences);
        std::cout << "Layer priority, independent audio, exact boundaries, settings, undo, atomic "
                     "rejection and v4 migration passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
