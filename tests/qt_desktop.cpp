#include "desktop/style.hpp"
#include "desktop/window.hpp"
#include "project/persistence.hpp"
#include "qt_font.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
#include <iostream>
using namespace nle;
using namespace nle::desktop;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error("check failed: " #x);                                         \
    } while (false)
template <class F> bool wait_for(F condition, int timeout = 10000) {
    QElapsedTimer clock;
    clock.start();
    while (!condition() && clock.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    return condition();
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    apply_style(app);
    prepare_test_font(app);
    try {
        const auto args = app.arguments();
        CHECK(args.size() == 6);
        Window window(args[1], args[2], false);
        window.show();
        window.importMedia(args[3] + "/av.mkv");
        CHECK(wait_for([&] { return !window.importing() && window.snapshot().media.size() == 1; }));
        CHECK(window.snapshot().sequences.empty());
        const auto imported = window.snapshot();
        window.findChild<QPushButton *>("appendButton")->click();
        CHECK(window.snapshot().sequences.size() == 1);
        CHECK(window.snapshot().sequences[0].tracks[0].clips.size() == 1);
        CHECK(window.snapshot().revision == imported.revision + 1);
        bool image = false;
        QObject::connect(&window.transport(), &Transport::frameReady, &app,
                         [&](const QImage &frame, qint64, qint64) {
                             if (!frame.isNull())
                                 image = true;
                         });
        CHECK(wait_for([&] { return !window.transport().loading() && image; }));
        window.setPlayhead({1});
        CHECK(wait_for([&] { return !window.transport().loading(); }));
        const auto beforeSplit = window.snapshot();
        window.findChild<QPushButton *>("splitButton")->click();
        CHECK(window.snapshot().sequences[0].tracks[0].clips.size() == 2);
        CHECK(window.snapshot().operations.back().actor.id.value == "human:desktop");
        window.findChild<QPushButton *>("undoButton")->click();
        CHECK(window.snapshot().sequences == beforeSplit.sequences);
        window.findChild<QPushButton *>("redoButton")->click();
        CHECK(window.snapshot().sequences[0].tracks[0].clips.size() == 2);
        const auto beforeInvalid = window.snapshot();
        window.findChild<QLineEdit *>("clipSourceIn")->setText("99");
        window.findChild<QPushButton *>("applyButton")->click();
        CHECK(window.snapshot() == beforeInvalid);
        // Restore inspector values through undo/redo, then persist the actual document.
        window.findChild<QPushButton *>("undoButton")->click();
        window.findChild<QPushButton *>("redoButton")->click();
        window.saveProject(args[4]);
        CHECK(load_project(media::utf8_path(args[4].toStdString())) == window.snapshot());
        const auto saved = window.snapshot();
        window.openProject(args[4]);
        CHECK(window.snapshot() == saved);
        window.setPlayhead({1, 2});
        image = false;
        CHECK(wait_for([&] { return !window.transport().loading() && image; }));
        const auto beforeStepping = window.snapshot();
        window.findChild<QPushButton *>("nextFrameButton")->click();
        CHECK(wait_for([&] { return !window.transport().loading(); }));
        CHECK((window.transport().position() > RationalTime{1, 2}));
        window.findChild<QPushButton *>("previousFrameButton")->click();
        CHECK(wait_for([&] { return !window.transport().loading(); }));
        CHECK((window.transport().position() == RationalTime{1, 2}));
        CHECK(window.snapshot() == beforeStepping);
        window.findChild<Timeline *>()->selected(
            window.snapshot().sequences[0].tracks[0].clips.front().id);
        const auto beforePlacement = window.snapshot();
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("placeDialog");
            dialog->findChild<QLineEdit *>("placementPosition")->setText("0");
            dialog->findChild<QLineEdit *>("placementDuration")->setText("1/2");
            dialog->accept();
        });
        window.findChild<QPushButton *>("placeButton")->click();
        CHECK(window.snapshot().sequences[0].tracks.size() == 2);
        CHECK(window.snapshot().sequences[0].tracks[0].clips.size() == 1);
        CHECK(window.snapshot().revision == beforePlacement.revision + 1);
        CHECK(wait_for([&] { return !window.transport().loading(); }));
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("playbackSettingsDialog");
            dialog->findChild<QCheckBox *>("trackMuted")->setChecked(true);
            dialog->findChild<QSpinBox *>("trackGain")->setValue(500);
            dialog->accept();
        });
        window.findChild<QPushButton *>("playbackSettingsButton")->click();
        CHECK(window.snapshot().sequences[0].tracks[0].playback.muted);
        CHECK(window.snapshot().sequences[0].tracks[0].playback.gain_milli == 500);
        window.findChild<QPushButton *>("undoButton")->click();
        CHECK(!window.snapshot().sequences[0].tracks[0].playback.muted);
        window.findChild<QPushButton *>("undoButton")->click();
        CHECK(window.snapshot().sequences == beforePlacement.sequences);
        window.saveProject(args[4]);
        QCoreApplication::processEvents();
        CHECK(window.grab().save(args[5]));
        window.transport().cancel();
        CHECK(wait_for([&] { return window.transport().idle(); }, 1000));
        window.close();
        std::cout << "Desktop import, append, preview, split, undo/redo, invalid edit, save/load "
                     "and rendering passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
