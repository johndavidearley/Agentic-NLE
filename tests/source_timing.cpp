#include "media/frame_index.hpp"
#include "project/persistence.hpp"
#include <iostream>
#include <limits>
using namespace nle;
using namespace nle::media;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error("check failed: " #x);                                         \
    } while (false)
template <class F> void rejects(F action) {
    bool rejected = false;
    try {
        action();
    } catch (const DomainError &) {
        rejected = true;
    }
    CHECK(rejected);
}
int main() {
    try {
        CHECK((SourceTime{-3, 2}.since(SourceTime{-2}) == RationalTime{1, 2}));
        CHECK((SourceTime{1, 3}.since(SourceTime{-1, 6}) == RationalTime{1, 2}));
        CHECK((SourceTime::from_ticks(-1024, {1, 48000}) == SourceTime{-8, 375}));
        CHECK(SourceTime{-1} < SourceTime{});
        rejects([] { (void)SourceTime{std::numeric_limits<std::int64_t>::min()}; });
        rejects([] { (void)SourceTime{-1}.since({}); });
        rejects(
            [] { (void)SourceTime::from_ticks(std::numeric_limits<std::int64_t>::max(), {2}); });
        const auto old = deserialize(R"nle(NLE_PROJECT 3
PROJECT 42 5 "Legacy"
MEDIA 1
ASSET 1 2 "AV" 4 1 1
LOCATION 0 "old.mkv"
SOURCE 1
METADATA "matroska" "test" "test" 100 13 2 2
STREAM 0 0 "ffv1" 1 1000 4000 1 2000 1 24 1 24 64 48 0 0
STREAM 1 1 "pcm_s16le" 1 1000 4000 1 2500 0 1 0 1 0 0 48000 1
SEQUENCES 1
SEQUENCE 2 "Main" 1 24 1
TRACK 3 0 "V1" 1
CLIP 4 1 0 1 0 1 3 1
AUDIT 0 0
END
)nle");
        CHECK(old.media[0].source->time_mode == SourceTimeMode::LegacyPerStream);
        CHECK(old.media[0].duration == RationalTime{4});
        CHECK(old.sequences[0].tracks[0].clips[0].source.start == RationalTime{});
        const auto migrated = serialize(old);
        CHECK(migrated.starts_with("NLE_PROJECT 5"));
        CHECK(deserialize(migrated) == old);
        auto shared = *old.media[0].source;
        shared.time_mode = SourceTimeMode::SharedOrigin;
        shared.container_start = SourceTime{2};
        CHECK(source_origin(shared) == SourceTime{2});
        CHECK((stream_offset(shared, shared.streams[1]) == RationalTime{1, 2}));
        CHECK((source_duration(shared) == RationalTime{9, 2}));
        Editor editor(old);
        editor.execute(ReplaceMediaSource{MediaId{1}, "new.mkv", shared});
        CHECK(editor.snapshot().media[0].source->time_mode == SourceTimeMode::LegacyPerStream);
        CHECK(editor.snapshot().sequences == old.sequences);
        CHECK(editor.undo());
        CHECK(editor.snapshot().media == old.media);
        Editor modern("Shared clock");
        const auto id = *modern
                             .execute(RegisterMedia{"AV",
                                                    MediaKind::AudioVideo,
                                                    source_duration(shared),
                                                    {{LocationRole::Original, "new.mkv"}},
                                                    shared})
                             .media;
        const auto before = modern.snapshot();
        auto unknown = shared;
        unknown.time_mode = SourceTimeMode::LegacyPerStream;
        rejects([&] { modern.execute(ReplaceMediaSource{id, "ambiguous.mkv", unknown}); });
        CHECK(modern.snapshot() == before);
        CHECK(deserialize(serialize(before)) == before);
        const auto index = parse_frame_index(
            "frame|best_effort_timestamp=2000|duration=16\nframe|best_effort_timestamp=2083|"
            "duration=16\nframe|best_effort_timestamp=2167|duration=16\n",
            shared);
        CHECK(index.frames.size() == 3);
        CHECK(index.at({1, 10}) == 1);
        CHECK(index.at({83, 1000}) == 1);
        CHECK(!index.at({183, 1000}));
        CHECK((index.step({1, 10}, 1, {{}, {1}}) == RationalTime{167, 1000}));
        CHECK(index.step({1, 10}, -1, {{}, {1}}) == RationalTime{});
        CHECK(!index.step({}, -1, {{}, {1}}));
        CHECK((index.step({1, 10}, -1, {{1, 20}, {1}}) == RationalTime{1, 20}));
        rejects([&] {
            (void)parse_frame_index("frame|best_effort_timestamp=2000|duration=1\nframe|best_"
                                    "effort_timestamp=2000|duration=1\n",
                                    shared);
        });
        rejects([&] { (void)parse_frame_index("frame|duration=1\n", shared); });
        rejects([&] { (void)parse_frame_index("frame|best_effort_timestamp=2000\n", shared); });
        rejects([&] {
            (void)parse_frame_index("frame|best_effort_timestamp=1999|duration=1\n", shared);
        });
        auto invalid = before;
        invalid.media[0].source->time_mode = static_cast<SourceTimeMode>(99);
        rejects([&] { validate(invalid); });
        std::cout << "Signed origins, legacy migration, relink clock policy, exact VFR lookup and "
                     "frame stepping passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
