#include "desktop/window.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
using namespace nle;
using namespace nle::desktop;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error("check failed: " #x);                                         \
    } while (false)
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        CHECK(app.arguments().size() == 3);
        QTemporaryDir scratch(app.arguments()[2] + "/qt-recovery-XXXXXX");
        CHECK(scratch.isValid());
        const auto project = scratch.path() + "/project.nle";
        const auto native = media::utf8_path(project.toStdString());
        Editor seed("Offline recovery");
        const auto media =
            *seed.execute(RegisterMedia{"Offline", MediaKind::Video, {60}, {}}).media;
        const auto sequence = *seed.execute(CreateSequence{"Main"}).sequence;
        const auto track = *seed.execute(CreateTrack{sequence, TrackKind::Video, "V1"}).track;
        const auto clip = *seed.execute(InsertClip{track, media, {}, {{}, {10}}}).clip;
        const auto saved = seed.snapshot();
        save_project(saved, native);
        ProjectSnapshot first;
        {
            Window window(app.arguments()[1], "ffprobe", false);
            window.openProject(project);
            window.findChild<Timeline *>()->selected(clip);
            window.findChild<QLineEdit *>("clipDuration")->setText("8");
            window.findChild<QPushButton *>("applyButton")->click();
            first = window.snapshot();
            CHECK(first.revision == saved.revision + 1);
            CHECK(window.checkpointRecovery());
            CHECK(load_project(native) == saved);
            window.findChild<QLineEdit *>("clipDuration")->setText("6");
            window.findChild<QPushButton *>("applyButton")->click();
            CHECK(window.checkpointRecovery());
            bool locked = false;
            try {
                DocumentFile other(native, true);
            } catch (const FileError &error) {
                locked = error.code == "project_locked";
            }
            CHECK(locked);
            // Destruction without a close/discard event models loss of the owning UI instance.
        }
        QFile damaged(project + ".recovery-1");
        CHECK(damaged.open(QIODevice::WriteOnly));
        damaged.write("truncated");
        damaged.close();
        {
            Window restored(app.arguments()[1], "ffprobe", false);
            restored.openProject(project, RecoveryChoice::Recover);
            CHECK(restored.snapshot() == first);
            CHECK(!restored.findChild<QPushButton *>("undoButton")->isEnabled());
            CHECK(restored.windowTitle().contains('*'));
            const auto copy = scratch.path() + "/copy.nle";
            restored.saveProject(copy);
            CHECK(load_project(media::utf8_path(copy.toStdString())) == first);
            CHECK(load_project(native) == saved);
            CHECK(!QFile::exists(project + ".recovery-0"));
            restored.close();
        }
        const auto drafts = scratch.path() + "/drafts";
        CHECK(QDir().mkpath(drafts));
        {
            DocumentFile draft(media::utf8_path((drafts + "/draft-prior.nle").toStdString()), true,
                               true);
            draft.checkpoint(first);
        }
        {
            Window restored(app.arguments()[1], "ffprobe", false, drafts);
            CHECK(restored.restoreDrafts(RecoveryChoice::Recover));
            CHECK(restored.snapshot() == first);
            restored.saveProject(scratch.path() + "/restored-draft.nle");
            restored.close();
        }
        {
            DocumentFile original(native, true);
            original.checkpoint(first);
        }
        {
            Window discarded(app.arguments()[1], "ffprobe", false);
            discarded.openProject(project, RecoveryChoice::Discard);
            CHECK(discarded.snapshot() == saved);
            CHECK(!QFile::exists(project + ".recovery-0"));
            discarded.close();
        }
        std::cout << "Desktop checkpoints, damaged-newest fallback, draft recovery, explicit "
                     "discard and Save As passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
