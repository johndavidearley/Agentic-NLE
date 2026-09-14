#include "desktop/transport.hpp"
#include "media/probe.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>
#include <algorithm>
#include <cmath>
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
playback::Plan plan_for(const std::filesystem::path &file, const std::filesystem::path &ffprobe) {
    const auto result = media::probe(file, ffprobe);
    Editor editor("Playback evaluation");
    const auto asset = *editor.execute(result.import_command()).media;
    const auto sequence = *editor.execute(CreateSequence{"Main"}).sequence;
    const auto kind =
        source_kind(result.source) == MediaKind::Audio ? TrackKind::Audio : TrackKind::Video;
    const auto track = *editor.execute(CreateTrack{sequence, kind, "Preview"}).track;
    (void)editor.execute(InsertClip{track, asset, {}, {{}, source_duration(result.source)}});
    return playback::make_plan(editor.snapshot(), sequence);
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        CHECK(args.size() == 6);
        const auto worker = args[1], ffprobe = args[2], directory = args[3];
        QJsonArray report;
        for (const auto *name : {"tone.wav", "video.mp4", "av.mkv", "vfr.mkv"}) {
            const auto plan = plan_for(media::utf8_path((directory + "/" + name).toStdString()),
                                       media::utf8_path(ffprobe.toStdString()));
            Transport transport(worker, false, nullptr, ffprobe);
            std::vector<QJsonObject> observations;
            qint64 pts = -1, finish = -1;
            int frames = 0;
            QObject::connect(&transport, &Transport::frameReady, &app,
                             [&](const QImage &image, qint64 start, qint64 end) {
                                 if (!image.isNull()) {
                                     pts = start;
                                     finish = end;
                                     ++frames;
                                 }
                             });
            QObject::connect(&transport, &Transport::observation, &app,
                             [&](const QJsonObject &message) { observations.push_back(message); });
            QElapsedTimer clock;
            clock.start();
            transport.open(plan);
            CHECK(wait_for([&] { return !transport.loading(); }));
            CHECK(transport.status() == "Paused");
            const bool video = plan.segments[0].asset.kind != MediaKind::Audio;
            if (video)
                CHECK(wait_for([&] { return frames > 0; }));
            const auto load_ms = clock.elapsed();
            const auto previous_frames = frames;
            clock.restart();
            transport.seek({1, 2});
            CHECK(wait_for(
                [&] { return !transport.loading() && (!video || frames > previous_frames); }));
            const auto seek_ms = clock.elapsed();
            if (video) {
                CHECK(pts >= 0 && pts <= 501000);
                CHECK(finish > 499000);
            }
            const auto seek_pts = pts, seek_end = finish;
            qint64 between_frame_pts = -1;
            if (QString::fromLatin1(name) == "vfr.mkv") {
                const auto before_hold = frames;
                transport.seek({11, 20});
                CHECK(wait_for([&] { return !transport.loading() && frames > before_hold; }, 3000));
                CHECK(pts == 500000);
                between_frame_pts = pts;
            }
            transport.play();
            CHECK(wait_for([&] { return transport.position() >= RationalTime{4, 5}; }, 3000));
            transport.pause();
            const auto paused = transport.position();
            QElapsedTimer pause_clock;
            pause_clock.start();
            wait_for([&] { return pause_clock.elapsed() >= 150; }, 300);
            CHECK(transport.position() == paused);
            // Restart from zero for steady-state delivery telemetry; timestamps are measured
            // at decoder output, not at the sound device or display scanout.
            observations.clear();
            transport.seek({});
            transport.play();
            CHECK(wait_for([&] { return transport.position() >= RationalTime{3, 4}; }, 4000));
            qint64 maximum_skew = 0;
            int pairs = 0;
            for (const auto &audio : observations) {
                if (audio["type"] != "audio" || audio["pts_us"].toInteger() < 250000)
                    continue;
                const QJsonObject *nearest = nullptr;
                for (const auto &frame : observations) {
                    if (frame["type"] != "video")
                        continue;
                    if (!nearest ||
                        std::abs(frame["pts_us"].toInteger() - audio["pts_us"].toInteger()) <
                            std::abs((*nearest)["pts_us"].toInteger() -
                                     audio["pts_us"].toInteger()))
                        nearest = &frame;
                }
                if (nearest && std::abs((*nearest)["pts_us"].toInteger() -
                                        audio["pts_us"].toInteger()) < 100000) {
                    const auto skew = std::abs(
                        (audio["wall_us"].toInteger() - (*nearest)["wall_us"].toInteger()) -
                        (audio["pts_us"].toInteger() - (*nearest)["pts_us"].toInteger()));
                    maximum_skew = std::max(maximum_skew, skew);
                    ++pairs;
                }
            }
            if (plan.segments[0].asset.kind == MediaKind::AudioVideo) {
                CHECK(pairs > 0);
                CHECK(maximum_skew < 200000);
            }
            clock.restart();
            transport.cancel();
            CHECK(wait_for([&] { return transport.idle(); }, 1000));
            const auto cancel_ms = clock.elapsed();
            CHECK(cancel_ms < 1000);
            report.append(
                QJsonObject{{"file", name},
                            {"load_ms", load_ms},
                            {"seek_ms", seek_ms},
                            {"seek_frame_pts_us", seek_pts},
                            {"seek_frame_end_us", seek_end},
                            {"vfr_between_frame_seek_us", between_frame_pts < 0 ? -1 : 550000},
                            {"vfr_between_frame_pts_us", between_frame_pts},
                            {"max_av_delivery_skew_us", maximum_skew},
                            {"av_pairs", pairs},
                            {"cancel_ms", cancel_ms}});
        }
        auto plan = plan_for(media::utf8_path((directory + "/video.mp4").toStdString()),
                             media::utf8_path(ffprobe.toStdString()));
        Transport latest(worker, false, nullptr, ffprobe);
        qint64 last = -1;
        QObject::connect(&latest, &Transport::frameReady, &app,
                         [&](const QImage &image, qint64 pts, qint64) {
                             if (!image.isNull())
                                 last = pts;
                         });
        latest.open(plan);
        for (int i = 0; i < 25; ++i)
            latest.seek({i, 50});
        CHECK(wait_for([&] { return last >= 450000 && !latest.loading(); }));
        latest.cancel();
        CHECK(wait_for([&] { return latest.idle(); }, 1000));
        plan.segments[0].asset.locations[0].uri = "does-not-exist.mp4";
        latest.open(plan);
        CHECK(latest.status().contains("missing"));
        CHECK(latest.idle());
        plan.segments[0].asset.source->time_mode = SourceTimeMode::LegacyPerStream;
        plan.segments[0].asset.source->streams[0].start_ticks = 1;
        latest.open(plan);
        CHECK(latest.status().contains("aligned"));
        plan = plan_for(media::utf8_path((directory + "/video.mp4").toStdString()),
                        media::utf8_path(ffprobe.toStdString()));
        Transport hanging(args[5], false, nullptr, ffprobe);
        hanging.setLoadTimeout(80);
        QElapsedTimer cancellation;
        cancellation.start();
        hanging.open(plan);
        CHECK(wait_for([&] { return hanging.status().contains("timed out") && hanging.idle(); },
                       1500));
        CHECK(cancellation.elapsed() < 1500);
        Transport absent(directory + "/missing-worker", false, nullptr, ffprobe);
        absent.open(plan);
        CHECK(wait_for([&] { return absent.status().contains("Cannot start") && absent.idle(); },
                       1500));
        auto corrupt = plan;
        corrupt.segments[0].asset.locations[0].uri = (directory + "/corrupt.wav").toStdString();
        corrupt.segments[0].asset.source->byte_size = std::filesystem::file_size(
            media::utf8_path(corrupt.segments[0].asset.locations[0].uri));
        latest.open(corrupt);
        CHECK(wait_for([&] { return !latest.loading() && latest.idle(); }, 3000));
        CHECK(latest.status() != "Paused");
        // Two cuts separated by a short silent gap must reach sequence end.
        auto cuts = plan;
        auto first = cuts.segments[0];
        first.source.duration = {1, 4};
        auto second = first;
        second.clip = ClipId{999};
        second.position = {1, 2};
        second.source.start = {1, 2};
        cuts.segments = {first, second};
        cuts.duration = {3, 4};
        latest.open(cuts);
        latest.play();
        CHECK(wait_for([&] { return latest.status() == "End of sequence"; }, 5000));
        CHECK(latest.position() == cuts.duration);
        CHECK(!latest.playing());
        auto encoded = QJsonDocument(report).toJson(QJsonDocument::Indented);
        QFile output(args[4]);
        CHECK(output.open(QIODevice::WriteOnly));
        output.write(encoded);
        std::cout << encoded.toStdString();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
