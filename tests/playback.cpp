#include "commands/editor.hpp"
#include "playback/plan.hpp"
#include <iostream>
using namespace nle;
using namespace nle::playback;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error("check failed: " #x);                                         \
    } while (false)
template <class F> void rejects(F f) {
    bool caught = false;
    try {
        f();
    } catch (const DomainError &) {
        caught = true;
    }
    CHECK(caught);
}
int main() {
    try {
        Editor editor("preview");
        const auto sequence = *editor.execute(CreateSequence{"Main"}).sequence;
        const auto track = *editor.execute(CreateTrack{sequence, TrackKind::Video, "V1"}).track;
        const auto media = *editor.execute(RegisterMedia{"test", MediaKind::Video, {10}, {}}).media;
        const auto clip = *editor.execute(InsertClip{track, media, {1}, {{2}, {3}}}).clip;
        (void)editor.execute(InsertClip{track, media, {5}, {{6}, {2}}});
        const auto plan = make_plan(editor.snapshot(), sequence);
        CHECK(plan.duration == RationalTime{7});
        CHECK(!plan.sample({0}).segment);
        CHECK(plan.sample({0}).next_boundary == RationalTime{1});
        CHECK(plan.sample({1}).source == RationalTime{2});
        CHECK((plan.sample({3, 2}).source == RationalTime{5, 2}));
        CHECK(!plan.sample({4}).segment);
        CHECK(plan.sample({5}).segment == 1);
        CHECK(!plan.sample({7}).segment);
        rejects([&] { (void)plan.sample({8}); });
        CHECK(milliseconds({1001, 30000}) == 33);
        CHECK(milliseconds({1, 48000}) == 0);
        rejects([] { (void)milliseconds({9223372036854775807LL}); });
        (void)editor.execute(MoveClip{clip, track, {0}});
        CHECK(plan.segments[0].position == RationalTime{1});
        CHECK(editor.undo());
        const auto audio = *editor.execute(CreateTrack{sequence, TrackKind::Audio, "A1"}).track;
        CHECK(make_plan(editor.snapshot(), sequence).segments.size() == 2);
        const auto sound =
            *editor.execute(RegisterMedia{"sound", MediaKind::Audio, {10}, {}}).media;
        (void)editor.execute(InsertClip{audio, sound, {0}, {{0}, {1}}});
        rejects([&] { (void)make_plan(editor.snapshot(), sequence); });
        SourceMetadata metadata{"test", "test", "test", 1, {10}, {}};
        SourceStream stream;
        stream.codec = "mpeg4";
        stream.time_base = {1, 24};
        stream.duration_ticks = 240;
        stream.start_known = true;
        stream.width = 64;
        stream.height = 48;
        metadata.streams.push_back(stream);
        MediaAsset asset{
            MediaId{1}, "test", MediaKind::Video, {10}, {{LocationRole::Original, "test.mp4"}},
            metadata};
        validate_preview_source(asset);
        asset.source->streams[0].start_ticks = 24;
        rejects([&] { validate_preview_source(asset); });
        asset.source->streams[0].start_ticks = 0;
        asset.source->streams[0].start_known = false;
        rejects([&] { validate_preview_source(asset); });
        auto beyondPreview = editor.snapshot();
        beyondPreview.sequences[0].tracks.pop_back();
        beyondPreview.media[0].duration = {100000};
        beyondPreview.sequences[0].tracks[0].clips[0].source.start = {90000};
        validate(beyondPreview);
        rejects([&] { (void)make_plan(beyondPreview, sequence); });
        auto tooShort = beyondPreview;
        tooShort.sequences[0].tracks[0].clips[0].source = {{9, 10000}, {1, 2000}};
        validate(tooShort);
        rejects([&] { (void)make_plan(tooShort, sequence); });
        asset.source.reset();
        rejects([&] { validate_preview_source(asset); });
        std::cout << "Exact preview mapping, gaps, bounds, isolation and unsupported-source checks "
                     "passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
