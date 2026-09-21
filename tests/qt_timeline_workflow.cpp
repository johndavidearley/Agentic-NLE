#include "desktop/media_cache.hpp"
#include "desktop/style.hpp"
#include "desktop/window.hpp"
#include "project/persistence.hpp"
#include "qt_font.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDragEnterEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QScrollBar>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <iostream>
using namespace nle;
using namespace nle::desktop;
#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__))                                                                        \
            throw std::runtime_error("check failed: " #__VA_ARGS__);                               \
    } while (false)
template <class F> bool waitFor(F f, int timeout = 12000) {
    QElapsedTimer timer;
    timer.start();
    while (!f() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    return f();
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    apply_style(app);
    prepare_test_font(app);
    try {
        const auto args = app.arguments();
        CHECK(args.size() == 5);
        QTemporaryDir recovery;
        CHECK(recovery.isValid());
        Window w(args[1], args[2], false, recovery.path());
        w.resize(1360, 900);
        w.show();
        auto *timeline = w.findChild<Timeline *>();
        auto *library = w.findChild<MediaLibrary *>();
        const auto click = [&](const char *name) {
            auto *b = w.findChild<QPushButton *>(name);
            CHECK(b && b->isEnabled());
            b->click();
        };
        const auto range = [&](const char *in, const char *out) {
            w.findChild<QLineEdit *>("sourceSelectionIn")->setText(in);
            w.findChild<QLineEdit *>("sourceSelectionOut")->setText(out);
        };
        const auto import = [&](const char *filename) {
            const auto count = w.snapshot().media.size();
            w.importMedia(args[3] + "/" + filename);
            CHECK(
                waitFor([&] { return !w.importing() && w.snapshot().media.size() == count + 1; }));
        };
        const auto place = [&](int position, int target) {
            QTimer::singleShot(0, &w, [&] {
                auto *dialog = w.findChild<QDialog *>("placeDialog");
                dialog->findChild<QLineEdit *>("placementPosition")
                    ->setText(QString::number(position));
                dialog->findChild<QComboBox *>("placementTrack")->setCurrentIndex(target);
                dialog->accept();
            });
            const auto revision = w.snapshot().revision;
            click("placeButton");
            CHECK(w.snapshot().revision == revision + 1);
        };
        import("video-a.mp4");
        range("0", "12");
        for (int i = 0; i < 10; ++i)
            click("appendButton");
        auto project = w.snapshot();
        CHECK(project.sequences[0].tracks[0].clips.size() == 10);
        CHECK(project.sequences[0].tracks[0].clips.back().position == RationalTime{108});
        import("video-b.mkv");
        range("1", "3");
        place(5, 0); // New top video track.
        timeline->setZoom(8);
        timeline->horizontalScrollBar()->setValue(0);
        w.findChild<QCheckBox *>("snapToggle")->setChecked(false);
        QCoreApplication::processEvents();
        // Real media drag/drop events use the current visible source-selection payload.
        for (int i = 1; i < 10; ++i) {
            const auto before = w.snapshot();
            const auto &seq = before.sequences[0];
            const auto &top = seq.tracks[0];
            QMimeData mime;
            mime.setData("application/x-agentic-nle-media", library->payload());
            const QPoint point(Timeline::headerWidth + (5 + i * 12) * 8,
                               Timeline::rulerHeight + 40);
            QDragEnterEvent enter(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(timeline->viewport(), &enter);
            CHECK(enter.isAccepted());
            QDragMoveEvent move(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(timeline->viewport(), &move);
            CHECK(move.isAccepted());
            CHECK(w.snapshot() == before);
            Editor direct(before);
            direct.execute(InsertClip{top.id, before.media[1].id, {5 + i * 12}, {{1}, {2}}});
            QDropEvent drop(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(timeline->viewport(), &drop);
            CHECK(drop.isAccepted() && w.snapshot().revision == before.revision + 1);
            CHECK(w.snapshot().sequences == direct.snapshot().sequences);
        }
        // An overlapping drop, a stale payload, and an abandoned drag publish nothing.
        const auto beforeBadDrop = w.snapshot();
        QMimeData badMime;
        badMime.setData("application/x-agentic-nle-media", library->payload());
        const QPoint badPoint(Timeline::headerWidth + 5 * 8, Timeline::rulerHeight + 40);
        QDragEnterEvent badEnter(badPoint, Qt::CopyAction, &badMime, Qt::LeftButton,
                                 Qt::NoModifier);
        QCoreApplication::sendEvent(timeline->viewport(), &badEnter);
        CHECK(badEnter.isAccepted());
        QDragMoveEvent badMove(badPoint, Qt::CopyAction, &badMime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(timeline->viewport(), &badMove);
        CHECK(!badMove.isAccepted());
        QDropEvent badDrop(badPoint, Qt::CopyAction, &badMime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(timeline->viewport(), &badDrop);
        CHECK(!badDrop.isAccepted());
        CHECK(w.snapshot() == beforeBadDrop);
        QDragEnterEvent abandoned(badPoint, Qt::CopyAction, &badMime, Qt::LeftButton,
                                  Qt::NoModifier);
        QCoreApplication::sendEvent(timeline->viewport(), &abandoned);
        QDragLeaveEvent leave;
        QCoreApplication::sendEvent(timeline->viewport(), &leave);
        CHECK(!timeline->gesturing() && w.snapshot() == beforeBadDrop);
        auto stale = QJsonDocument::fromJson(library->payload()).object();
        stale["revision"] = "0";
        QMimeData staleMime;
        staleMime.setData("application/x-agentic-nle-media", QJsonDocument(stale).toJson());
        QDragEnterEvent staleEnter(badPoint, Qt::CopyAction, &staleMime, Qt::LeftButton,
                                   Qt::NoModifier);
        QCoreApplication::sendEvent(timeline->viewport(), &staleEnter);
        CHECK(!staleEnter.isAccepted() && w.snapshot() == beforeBadDrop);
        import("audio-0.wav");
        range("0", "12");
        for (int i = 0; i < 10; ++i)
            click("appendButton");
        import("audio-1.wav");
        range("0", "12");
        place(0, 0);
        for (int i = 1; i < 10; ++i)
            place(i * 12, 2); // Existing second audio track, after 'new' and dialogue.
        project = w.snapshot();
        CHECK(project.sequences[0].tracks.size() == 4);
        for (const auto &track : project.sequences[0].tracks)
            CHECK(track.clips.size() == 10);
        CHECK(project.sequences[0].tracks[3].clips.back().position +
                  project.sequences[0].tracks[3].clips.back().source.duration ==
              RationalTime{120});
        const auto original = project.sequences;
        QTimer::singleShot(0, &w, [&] {
            auto *dialog = w.findChild<QDialog *>("sequenceSettingsDialog");
            dialog->findChild<QSpinBox *>("sequenceWidth")->setValue(1280);
            dialog->findChild<QSpinBox *>("sequenceHeight")->setValue(720);
            dialog->findChild<QComboBox *>("sequenceFps")->setCurrentText("30000/1001");
            dialog->accept();
        });
        click("sequenceSettingsButton");
        CHECK(w.snapshot().sequences[0].frame_duration == RationalTime(1001, 30000));
        CHECK(w.snapshot().sequences[0].output.width == 1280);
        CHECK(w.snapshot().sequences[0].tracks == original[0].tracks);
        click("undoButton");
        CHECK(w.snapshot().sequences == original);
        click("redoButton");
        // Source selection is validated before adding an operation.
        range("3", "2");
        const auto beforeInvalid = w.snapshot();
        click("appendButton");
        CHECK(w.snapshot() == beforeInvalid);
        range("0", "12");
        // Header controls are visible and apply one undoable track command.
        timeline->verticalScrollBar()->setValue(timeline->verticalScrollBar()->maximum());
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const auto track = w.snapshot().sequences[0].tracks[3].id;
        const auto suffix = QString::number(track.value);
        auto *gain = timeline->findChild<QSpinBox *>("trackGain_" + suffix);
        CHECK(gain && gain->isVisible());
        gain->setValue(300);
        CHECK(w.snapshot().sequences[0].tracks[3].playback.gain_milli == 300);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        auto *mute = timeline->findChild<QCheckBox *>("trackMuted_" + suffix);
        CHECK(mute && mute->isVisible());
        mute->click();
        CHECK(w.snapshot().sequences[0].tracks[3].playback.muted);
        click("undoButton");
        CHECK(!w.snapshot().sequences[0].tracks[3].playback.muted);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        auto *up = timeline->findChild<QPushButton *>("trackUp_" + suffix);
        CHECK(up && up->isVisible());
        up->click();
        CHECK(w.snapshot().sequences[0].tracks[2].id == track);
        click("undoButton");
        CHECK(w.snapshot().sequences[0].tracks[3].id == track);
        w.setPlayhead({6});
        CHECK(waitFor([&] { return !w.transport().loading(); }));
        w.saveProject(args[4] + ".nle");
        CHECK(load_project(media::utf8_path((args[4] + ".nle").toStdString())) == w.snapshot());
        timeline->setZoom(8);
        timeline->horizontalScrollBar()->setValue(0);
        timeline->verticalScrollBar()->setValue(0);
        CHECK(waitFor([&] { return w.findChild<MediaCache *>()->pending() == 0; }));
        QCoreApplication::processEvents();
        CHECK(w.grab().save(args[4] + ".png"));
        // Every main input remains inside the window at this display scale.
        for (const char *name :
             {"sourceSelectionIn", "sourceSelectionOut", "sequenceSettingsButton", "appendButton",
              "placeButton", "timelineZoom"}) {
            const auto *widget = w.findChild<QWidget *>(name);
            CHECK(widget && widget->isVisible());
            CHECK(w.rect().contains(QRect(widget->mapTo(&w, QPoint{}), widget->size())));
        }
        w.transport().cancel();
        CHECK(waitFor([&] { return w.transport().idle(); }, 1500));
        w.close();
        std::cout << "Two-minute reference: 40 clips, B-roll, dialogue, music, source ranges, "
                     "drops, settings, headers and save/load passed\n";
        return 0;
    } catch (const std::exception &x) {
        std::cerr << x.what() << '\n';
        return 1;
    }
}
