#include "desktop/style.hpp"
#include "desktop/timeline.hpp"
#include "qt_font.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <algorithm>
#include <iostream>
using namespace nle;
using namespace nle::desktop;
#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__))                                                                        \
            throw std::runtime_error("check failed: " #__VA_ARGS__);                               \
    } while (false)
void mouse(Timeline &t, QEvent::Type type, QPointF at) {
    QMouseEvent event(type, at, QPointF(t.viewport()->mapToGlobal(at.toPoint())),
                      type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(t.viewport(), &event);
}
void key(Timeline &t, int k) {
    QKeyEvent event(QEvent::KeyPress, k, Qt::NoModifier);
    QCoreApplication::sendEvent(&t, &event);
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    apply_style(app);
    prepare_test_font(app);
    try {
        Editor e("Timeline");
        auto s = *e.execute(CreateSequence{"Main", {1001, 30000}}).sequence;
        auto t = *e.execute(CreateTrack{s, TrackKind::Video, "V1"}).track;
        auto v = *e.execute(CreateTrack{s, TrackKind::Video, "V2"}).track;
        auto a = *e.execute(CreateTrack{s, TrackKind::Audio, "A1"}).track;
        (void)a;
        auto m = *e.execute(RegisterMedia{"Offline source", MediaKind::Video, {20}, {}}).media;
        auto c = *e.execute(InsertClip{t, m, {2}, {{2}, {3}}}).clip;
        e.execute(InsertClip{t, m, {8}, {{0}, {3}}});
        Timeline view;
        view.resize(1360, 540);
        view.display(e.snapshot(), s, c);
        view.show();
        view.setZoom(80);
        view.horizontalScrollBar()->setValue(0);
        view.setSnapping(false);
        QCoreApplication::processEvents();
        int commits = 0, rejected = 0;
        ClipId selected = c;
        QObject::connect(&view, &Timeline::selected, &app, [&](ClipId id) { selected = id; });
        QObject::connect(&view, &Timeline::rejected, &app, [&](const QString &) { ++rejected; });
        QObject::connect(&view, &Timeline::editRequested, &app,
                         [&](const Command &cmd, std::uint64_t rev, const QString &label) {
                             e.execute(cmd, {{}, label.toStdString(), rev});
                             ++commits;
                             view.display(e.snapshot(), s, selected);
                         });
        const auto refresh = [&] {
            view.display(e.snapshot(), s, c);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        };
        const auto drag = [&](QPointF start, QPointF end) {
            mouse(view, QEvent::MouseButtonPress, start);
            const auto before = e.snapshot();
            for (int i = 1; i <= 5; ++i) {
                mouse(view, QEvent::MouseMove, start + (end - start) * (i / 5.0));
                CHECK(e.snapshot() == before);
            }
            mouse(view, QEvent::MouseButtonRelease, end);
        };
        auto original = e.snapshot();
        Editor direct(original);
        direct.execute(MoveClip{c, v, {3}});
        auto center = view.clipRect(c).center();
        drag(center, center + QPointF(80, Timeline::rowHeight));
        CHECK(commits == 1 && e.snapshot().sequences == direct.snapshot().sequences);
        CHECK(e.undo());
        refresh();
        CHECK(e.snapshot().sequences == original.sequences);
        CHECK(e.redo());
        CHECK(e.undo());
        refresh();
        // Neither a rejected overlap nor incompatible track changes the project.
        auto before = e.snapshot();
        center = view.clipRect(c).center();
        drag(center, center + QPointF(6 * 80, 0));
        CHECK(e.snapshot() == before && rejected == 1);
        drag(center, center + QPointF(0, 2 * Timeline::rowHeight));
        CHECK(e.snapshot() == before && rejected == 2);
        drag(center, QPointF(Timeline::headerWidth - 20, center.y()));
        CHECK(e.snapshot() == before && rejected == 3);
        const auto invalidBox = view.clipRect(c);
        drag(QPointF(invalidBox.right() - 2, invalidBox.center().y()),
             QPointF(invalidBox.left(), invalidBox.center().y()));
        CHECK(e.snapshot() == before && rejected == 4);
        // Escape and external edits cancel gestures before release.
        mouse(view, QEvent::MouseButtonPress, center);
        mouse(view, QEvent::MouseMove, center + QPointF(80, 0));
        key(view, Qt::Key_Escape);
        mouse(view, QEvent::MouseButtonRelease, center + QPointF(80, 0));
        CHECK(!view.gesturing() && e.snapshot() == before);
        mouse(view, QEvent::MouseButtonPress, center);
        mouse(view, QEvent::MouseMove, center + QPointF(80, 0));
        e.execute(SetTrackPlayback{t, {true, true, 1000}});
        before = e.snapshot();
        refresh();
        mouse(view, QEvent::MouseButtonRelease, center + QPointF(80, 0));
        CHECK(e.snapshot() == before && rejected == 5);
        auto box = view.clipRect(c);
        const auto trimBefore = e.snapshot();
        drag(QPointF(box.left() + 2, box.center().y()), QPointF(box.left() + 80, box.center().y()));
        CHECK(e.snapshot().sequences[0].tracks[0].clips[0].source == TimeRange({3}, {2}));
        CHECK(e.undo());
        refresh();
        box = view.clipRect(c);
        drag(QPointF(box.right() - 2, box.center().y()),
             QPointF(box.right() - 80, box.center().y()));
        CHECK(e.snapshot().sequences[0].tracks[0].clips[0].source == TimeRange({2}, {2}));
        CHECK(e.undo());
        refresh();
        CHECK(e.snapshot().sequences == trimBefore.sequences);
        view.setPosition({3});
        key(view, Qt::Key_S);
        CHECK(e.snapshot().sequences[0].tracks[0].clips.size() == 3);
        key(view, Qt::Key_Delete);
        CHECK(e.snapshot().sequences[0].tracks[0].clips.size() == 2);
        CHECK(e.undo() && e.undo());
        refresh();
        key(view, Qt::Key_BracketRight);
        CHECK(selected != c);
        key(view, Qt::Key_BracketLeft);
        CHECK(selected == c);
        view.setSnapping(true);
        view.setPosition({50});
        center = view.clipRect(c).center();
        drag(center, center + QPointF(80.072, Timeline::rowHeight));
        CHECK(e.snapshot().sequences[0].tracks[1].clips[0].position == RationalTime(3003, 1000));
        // A thousand clips built in one transaction, without quadratic retained history.
        Editor large("1000 clips", {0, 0});
        auto tx = large.begin();
        auto ls = *tx.execute(CreateSequence{"Large", {1, 30}}).sequence;
        auto lt = *tx.execute(CreateTrack{ls, TrackKind::Video, "V1"}).track;
        auto lm = *tx.execute(RegisterMedia{"Synthetic", MediaKind::Video, {10}, {}}).media;
        ClipId first;
        for (int i = 0; i < 1000; ++i) {
            const auto id = *tx.execute(InsertClip{lt, lm, {i * 3}, {{}, {2}}}).clip;
            if (i == 0)
                first = id;
        }
        large.commit(std::move(tx));
        view.display(large.snapshot(), ls, first);
        view.setSnapping(true);
        view.horizontalScrollBar()->setValue(0);
        QCoreApplication::processEvents();
        center = view.clipRect(first).center();
        QElapsedTimer clock;
        clock.start();
        mouse(view, QEvent::MouseButtonPress, center);
        const double pressMs = static_cast<double>(clock.nsecsElapsed()) / 1000000.0;
        std::vector<double> samples;
        for (int i = 0; i < 120; ++i) {
            clock.restart();
            mouse(view, QEvent::MouseMove, center + QPointF(20 + i % 40, 0));
            view.viewport()->repaint();
            samples.push_back(static_cast<double>(clock.nsecsElapsed()) / 1000000.0);
        }
        key(view, Qt::Key_Escape);
        CHECK(large.snapshot().revision == 1);
        std::sort(samples.begin(), samples.end());
        QJsonObject report{{"clips", 1000},
                           {"iterations", 120},
                           {"press_ms", pressMs},
                           {"gesture_p50_ms", samples[60]},
                           {"gesture_p95_ms", samples[114]},
                           {"gesture_max_ms", samples.back()},
                           {"scale", view.devicePixelRatioF()},
                           {"commits", commits},
                           {"rejections", rejected}};
        const auto json = QJsonDocument(report).toJson();
        std::cout << json.constData();
        if (app.arguments().size() > 1) {
            QFile file(app.arguments()[1]);
            CHECK(file.open(QIODevice::WriteOnly));
            CHECK(file.write(json) == json.size());
        }
        if (app.arguments().size() > 2)
            CHECK(view.grab().save(app.arguments()[2]));
        return 0;
    } catch (const std::exception &x) {
        std::cerr << x.what() << '\n';
        return 1;
    }
}
