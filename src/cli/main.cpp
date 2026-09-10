#include "commands/editor.hpp"
#include "project/persistence.hpp"
#include <filesystem>
#include <iostream>
#include <string_view>

namespace {
using namespace nle;
void inspect(const ProjectSnapshot &project) {
    std::cout << "Project " << project.id.value << " \"" << project.name << "\"\n";
    std::cout << "revision=" << project.revision << " operations=" << project.operations.size()
              << '\n';
    std::cout << "media=" << project.media.size() << " sequences=" << project.sequences.size()
              << '\n';
    for (const auto &sequence : project.sequences) {
        std::cout << "Sequence " << sequence.id.value << " \"" << sequence.name << "\"\n";
        for (const auto &track : sequence.tracks) {
            std::cout << "  Track " << track.id.value << ' '
                      << (track.kind == TrackKind::Video ? "video" : "audio") << " \"" << track.name
                      << "\" clips=" << track.clips.size() << '\n';
            for (const auto &clip : track.clips)
                std::cout << "    Clip " << clip.id.value << " media=" << clip.media.value
                          << " at=" << clip.position.value() << '/' << clip.position.rate()
                          << " source=" << clip.source.start.value() << '/'
                          << clip.source.start.rate()
                          << " duration=" << clip.source.duration.value() << '/'
                          << clip.source.duration.rate() << '\n';
        }
    }
}
ProjectSnapshot demo() {
    Editor editor("Headless timeline demo");
    const auto sequence = *editor.execute(CreateSequence{"Main", {1001, 24000}}).sequence;
    const auto video = *editor.execute(CreateTrack{sequence, TrackKind::Video, "V1"}).track;
    (void)editor.execute(CreateTrack{sequence, TrackKind::Audio, "A1"});
    const auto media = *editor
                            .execute(RegisterMedia{"Offline demo asset",
                                                   MediaKind::AudioVideo,
                                                   {60},
                                                   {{LocationRole::Original, "demo.mov"}}})
                            .media;
    const auto clip = *editor.execute(InsertClip{video, media, {0}, {{5}, {10}}}).clip;
    (void)editor.execute(MoveClip{clip, video, {2}});
    (void)editor.execute(TrimClip{clip, {2}, {{6}, {8}}});
    const auto right = *editor.execute(SplitClip{clip, {6}}).clip;
    (void)editor.execute(DeleteClip{right});
    if (!editor.undo() || !editor.redo() || !editor.undo())
        throw DomainError("demo undo/redo failed");
    return editor.snapshot();
}
ProjectSnapshot session_demo() {
    Editor editor("Transactional timeline demo");
    const auto empty = editor.snapshot();
    auto batch = editor.begin({{ActorId{"agent:cli-demo"}, ActorKind::Agent},
                               "Build and trim a sequence",
                               editor.revision()});
    const auto sequence = *batch.execute(CreateSequence{"Main"}).sequence;
    const auto track = *batch.execute(CreateTrack{sequence, TrackKind::Video, "V1"}).track;
    const auto media =
        *batch.execute(RegisterMedia{"Offline asset", MediaKind::Video, {60}, {}}).media;
    const auto clip = *batch.execute(InsertClip{track, media, {0}, {{0}, {10}}}).clip;
    (void)batch.execute(TrimClip{clip, {2}, {{1}, {8}}});
    (void)batch.execute(SplitClip{clip, {6}});
    (void)batch.execute(RelinkMedia{media, LocationRole::Original, "demo.mov"});
    const auto preview = batch.preview();
    if (editor.snapshot() != empty || preview.sequences.size() != 1)
        throw DomainError("preview isolation failed");
    if (!editor.commit(std::move(batch)) || editor.revision() != 1 || !editor.undo() ||
        !editor.snapshot().sequences.empty() || !editor.redo())
        throw DomainError("batch commit/undo/redo failed");
    std::cout << "Batch preview, commit, undo and redo passed\n";
    return editor.snapshot();
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc < 3) {
            std::cerr << "Usage: editor-cli new FILE [NAME] | add-sequence FILE NAME | inspect FILE"
                         " | demo FILE | session-demo FILE\n";
            return 2;
        }
        const std::string_view operation = argv[1];
        const std::filesystem::path path = argv[2];
        if (operation == "new" && (argc == 3 || argc == 4)) {
            if (std::filesystem::exists(path))
                throw nle::DomainError("new requires a path that does not exist");
            nle::Editor editor(argc == 4 ? argv[3] : "Untitled");
            nle::save_project(editor.snapshot(), path);
        } else if (operation == "add-sequence" && argc == 4) {
            nle::Editor editor(nle::load_project(path));
            (void)editor.execute(nle::CreateSequence{argv[3]});
            nle::save_project(editor.snapshot(), path);
        } else if (operation == "inspect" && argc == 3) {
            inspect(nle::load_project(path));
        } else if ((operation == "demo" || operation == "session-demo") && argc == 3) {
            auto editor = operation == "demo" ? demo() : session_demo();
            nle::save_project(editor, path);
            const auto reloaded = nle::load_project(path);
            if (editor != reloaded)
                throw nle::DomainError("demo round-trip failed");
            inspect(reloaded);
        } else {
            throw nle::DomainError("unknown operation or incorrect argument count");
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "editor-cli: " << error.what() << '\n';
        return 1;
    }
}
