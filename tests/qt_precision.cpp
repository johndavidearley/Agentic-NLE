#include "desktop/transport.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>
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
qint64 us(RationalTime value) { return value.value() * 1000000 / value.rate(); }
playback::Plan make(const QString &file, const QString &probe) {
    const auto result =
        media::probe(media::utf8_path(file.toStdString()), media::utf8_path(probe.toStdString()));
    Editor editor("Precision evaluation");
    const auto asset = *editor.execute(result.import_command()).media;
    const auto sequence = *editor.execute(CreateSequence{"Main"}).sequence;
    const auto track = *editor.execute(CreateTrack{sequence, TrackKind::Video, "V1"}).track;
    editor.execute(InsertClip{track, asset, {}, {{}, source_duration(result.source)}});
    return playback::make_plan(editor.snapshot(), sequence);
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        CHECK(args.size() == 6);
        const auto worker = args[1], probe = args[2], directory = args[3];
        QJsonArray report;
        for (const auto *name : {"vfr.mkv", "bframes.mp4", "offset-common.mkv", "offset-audio.mkv",
                                 "offset-video.mkv", "negative-origin.mkv"}) {
            std::cerr << "Checking " << name << '\n';
            const auto plan = make(directory + "/" + name, probe);
            const auto &source = *plan.segments[0].asset.source;
            CHECK(source.time_mode == SourceTimeMode::SharedOrigin);
            const auto index =
                media::index_frames(media::utf8_path((directory + "/" + name).toStdString()),
                                    source, media::utf8_path(probe.toStdString()));
            Transport transport(worker, false, nullptr, probe);
            qint64 pts = -1;
            int frames = 0;
            QObject::connect(&transport, &Transport::frameReady, &app,
                             [&](const QImage &image, qint64 value, qint64) {
                                 if (!image.isNull()) {
                                     pts = value;
                                     ++frames;
                                 }
                             });
            transport.open(plan);
            CHECK(wait_for([&] { return !transport.loading(); }));
            if (transport.status() != "Paused")
                throw std::runtime_error(std::string(name) + ": " +
                                         transport.status().toStdString());
            if (QString::fromLatin1(name) == "offset-video.mkv") {
                CHECK(frames == 0);
                CHECK((index.frames.front().position == RationalTime{1, 2}));
            }
            qint64 maximum_error = 0;
            for (const auto target :
                 {RationalTime{11, 20}, RationalTime{913, 1000}, RationalTime{137, 100}}) {
                const auto selected = index.at(target);
                CHECK(selected.has_value());
                const auto before = frames;
                transport.seek(target);
                CHECK(wait_for([&] { return !transport.loading() && frames > before; }));
                CHECK(transport.position() == target);
                const auto error = std::abs(pts - us(index.frames[*selected].position));
                CHECK(error <= 2);
                maximum_error = std::max(maximum_error, error);
            }
            transport.seek({11, 20});
            CHECK(wait_for([&] { return !transport.loading(); }));
            for (int direction : {1, 1, 1, -1, -1, -1}) {
                const auto expected =
                    index.step(transport.position(), direction, plan.segments[0].source);
                CHECK(expected.has_value());
                const auto before = frames;
                transport.step(direction);
                CHECK(wait_for([&] { return !transport.loading() && frames > before; }));
                CHECK(transport.position() == *expected);
                CHECK(std::abs(pts - us(index.frames[*index.at(*expected)].position)) <= 2);
            }
            transport.seek(plan.duration);
            CHECK(wait_for([&] { return !transport.loading(); }));
            const auto last =
                index.step(plan.segments[0].source.end(), -1, plan.segments[0].source);
            CHECK(last.has_value());
            transport.step(-1);
            CHECK(wait_for([&] { return !transport.loading(); }));
            CHECK(transport.position() == *last);
            transport.cancel();
            CHECK(wait_for([&] { return transport.idle(); }, 1000));
            report.append(QJsonObject{{"file", name},
                                      {"frame_count", static_cast<qint64>(index.frames.size())},
                                      {"max_decoded_pts_error_us", maximum_error},
                                      {"step_checks", 7}});
        }
        {
            const auto imported =
                media::probe(media::utf8_path((directory + "/negative-audio.mkv").toStdString()),
                             media::utf8_path(probe.toStdString()));
            CHECK(source_origin(imported.source).negative());
            Editor editor("Negative audio");
            const auto asset = *editor.execute(imported.import_command()).media;
            const auto sequence = *editor.execute(CreateSequence{"Main"}).sequence;
            const auto track = *editor.execute(CreateTrack{sequence, TrackKind::Audio, "A1"}).track;
            editor.execute(InsertClip{track, asset, {}, {{}, {4}}});
            Transport audio(worker, false, nullptr, probe);
            qint64 first = -1;
            QObject::connect(&audio, &Transport::observation, &app,
                             [&](const QJsonObject &message) {
                                 if (message["type"] == "audio" && first < 0)
                                     first = message["pts_us"].toInteger();
                             });
            audio.open(playback::make_plan(editor.snapshot(), sequence));
            CHECK(wait_for([&] { return !audio.loading(); }));
            CHECK(audio.status() == "Paused");
            audio.seek({1, 4});
            CHECK(wait_for([&] { return !audio.loading(); }));
            first = -1;
            audio.play();
            CHECK(wait_for([&] { return audio.position() >= RationalTime{3, 4}; }, 3000));
            CHECK(first >= 200000 && first <= 270000);
            audio.cancel();
            CHECK(wait_for([&] { return audio.idle(); }, 1000));
            report.append(QJsonObject{{"file", "negative-audio.mkv"},
                                      {"first_audio_after_quarter_second_seek_us", first}});
        }
        // A direct jump to the end of another asset must not step using the old cache.
        {
            auto plan = make(directory + "/vfr.mkv", probe);
            auto tail = make(directory + "/offset-video.mkv", probe).segments.front();
            tail.position = plan.duration;
            plan.duration = tail.end();
            plan.segments.push_back(tail);
            Transport stepping(worker, false, nullptr, probe);
            stepping.open(plan);
            CHECK(wait_for([&] { return !stepping.loading(); }));
            stepping.seek(plan.duration);
            CHECK(wait_for([&] { return !stepping.loading(); }));
            stepping.step(-1);
            CHECK(stepping.position() == plan.duration);
            CHECK(stepping.status().contains("Seek into this clip"));
            stepping.seek(tail.position + RationalTime{1});
            CHECK(wait_for([&] { return !stepping.loading(); }));
            CHECK(stepping.status() == "Paused");
            stepping.seek(plan.duration);
            CHECK(wait_for([&] { return !stepping.loading(); }));
            stepping.step(-1);
            CHECK(wait_for([&] { return !stepping.loading(); }));
            CHECK(stepping.position() >= tail.position && stepping.position() < plan.duration);
            stepping.cancel();
            CHECK(wait_for([&] { return stepping.idle(); }, 1000));
        }
        // Indexing uses the supervised process runner and remains independently cancellable.
        const auto short_plan = make(directory + "/vfr.mkv", probe);
        Transport indexing(worker, false, nullptr, args[5]);
        indexing.open(short_plan);
        CHECK(indexing.loading());
        QElapsedTimer cancel;
        cancel.start();
        indexing.cancel();
        CHECK(wait_for([&] { return indexing.idle(); }, 1000));
        report.append(QJsonObject{{"index_cancel_ms", cancel.elapsed()}});
        // Observe actual delayed-audio scheduling and drift over a 12-second source.
        const auto long_plan = make(directory + "/long-av.mkv", probe);
        CHECK(long_plan.duration == RationalTime{12});
        Transport transport(worker, false, nullptr, probe);
        std::vector<QJsonObject> observations;
        QObject::connect(&transport, &Transport::observation, &app,
                         [&](const QJsonObject &message) {
                             if (message["type"] == "video" || message["type"] == "audio") {
                                 auto copy = message;
                                 copy.remove("image");
                                 observations.push_back(copy);
                             }
                         });
        transport.open(long_plan);
        // Keep the measured playback deadline independent of asynchronous indexing/loading.
        CHECK(wait_for([&] { return !transport.loading(); }, 35000));
        CHECK(transport.status() == "Paused");
        observations.clear();
        transport.play();
        if (!wait_for([&] { return transport.position() >= RationalTime{11}; }, 16000)) {
            std::cerr << "Long playback failed: status=" << transport.status().toStdString()
                      << " position=" << transport.position().value() << '/'
                      << transport.position().rate() << " observations=" << observations.size()
                      << '\n';
            throw std::runtime_error("long playback did not reach 11 seconds in 16 seconds");
        }
        qint64 first_audio = -1, first_video = -1;
        double early = 0, late = 0;
        int early_count = 0, late_count = 0;
        qint64 max_skew = 0;
        for (const auto &sample : observations) {
            if (sample["type"] == "video" && first_video < 0)
                first_video = sample["pts_us"].toInteger();
            if (sample["type"] != "audio")
                continue;
            const auto pts = sample["pts_us"].toInteger();
            if (first_audio < 0)
                first_audio = pts;
            const QJsonObject *nearest = nullptr;
            for (const auto &video : observations) {
                if (video["type"] != "video")
                    continue;
                if (!nearest || std::abs(video["pts_us"].toInteger() - pts) <
                                    std::abs((*nearest)["pts_us"].toInteger() - pts))
                    nearest = &video;
            }
            if (!nearest || std::abs((*nearest)["pts_us"].toInteger() - pts) > 50000)
                continue;
            const auto skew = (sample["wall_us"].toInteger() - (*nearest)["wall_us"].toInteger()) -
                              (pts - (*nearest)["pts_us"].toInteger());
            max_skew = std::max(max_skew, static_cast<qint64>(std::abs(skew)));
            if (pts >= 1000000 && pts < 2000000) {
                early += static_cast<double>(skew);
                ++early_count;
            }
            if (pts >= 10000000) {
                late += static_cast<double>(skew);
                ++late_count;
            }
        }
        CHECK(first_video == 0);
        CHECK(first_audio >= 499000 && first_audio <= 501000);
        CHECK(early_count > 10 && late_count > 10);
        CHECK(max_skew < 200000);
        const auto drift = std::abs(late / late_count - early / early_count);
        CHECK(drift < 100000);
        transport.cancel();
        CHECK(wait_for([&] { return transport.idle(); }, 1000));
        report.append(QJsonObject{{"file", "long-av.mkv"},
                                  {"first_video_us", first_video},
                                  {"first_audio_us", first_audio},
                                  {"early_pairs", early_count},
                                  {"late_pairs", late_count},
                                  {"max_delivery_skew_us", max_skew},
                                  {"mean_offset_drift_us", drift}});
        QFile output(args[4]);
        CHECK(output.open(QIODevice::WriteOnly));
        const auto encoded = QJsonDocument(report).toJson(QJsonDocument::Indented);
        CHECK(output.write(encoded) == encoded.size());
        std::cout << encoded.toStdString();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
