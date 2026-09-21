#include "decode/renderer.hpp"
#include "desktop/sequence_transport.hpp"
#include "media/frame_index.hpp"
#include "media/probe.hpp"
#include "project/persistence.hpp"
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
using namespace nle;
namespace {
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void wait(F done, int timeout = 10000) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    check(done(), "Wait timed out");
}
qint64 us(RationalTime t) { return t.value() * 1000000 / t.rate(); }
std::int64_t source_sample(std::int64_t timeline) {
    constexpr std::array<std::int64_t, 3> starts{0, 6 * 48000, 2 * 48000};
    return starts[static_cast<std::size_t>(timeline / (4 * 48000) % 3)] + timeline % (4 * 48000);
}
RationalTime source_time(RationalTime timeline) {
    const auto cut = timeline.value() / timeline.rate() / 4;
    constexpr std::array<int, 3> starts{0, 6, 2};
    return RationalTime{starts[static_cast<std::size_t>(cut % 3)]} +
           (timeline - RationalTime{cut * 4});
}
std::vector<std::int16_t> wave(const std::filesystem::path &file) {
    std::ifstream in(file, std::ios::binary);
    in.seekg(44);
    std::vector<std::int16_t> result(12 * 48000 * 2);
    in.read(reinterpret_cast<char *>(result.data()),
            static_cast<std::streamsize>(result.size() * 2));
    check(bool(in), "Reference WAV missing");
    return result;
}
double percentile(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(.95 * static_cast<double>(values.size() - 1))];
}
const std::array<std::string, 6> names{"video-a.mp4", "video-b.mkv", "audio-0.wav",
                                       "audio-1.wav", "audio-2.wav", "audio-3.wav"};
struct Reference {
    ProjectSnapshot project;
    SequenceId sequence;
    std::array<media::FrameIndex, 2> indexes;
    std::array<std::vector<std::int16_t>, 4> audio;
};
Reference reference(const std::filesystem::path &root, const std::filesystem::path &probe,
                    int seconds) {
    Editor editor("Multi-track reference");
    Reference result;
    result.sequence = *editor.execute(CreateSequence{"Main", {1001, 30000}}).sequence;
    std::array<MediaId, 6> assets;
    std::array<TrackId, 6> tracks;
    for (std::size_t i = 0; i < 6; ++i) {
        const auto source = media::probe(root / names[i], probe);
        assets[i] = *editor.execute(source.import_command()).media;
        tracks[i] =
            *editor
                 .execute(CreateTrack{result.sequence, i < 2 ? TrackKind::Video : TrackKind::Audio,
                                      names[i]})
                 .track;
        if (i < 2)
            result.indexes[i] = media::index_frames(root / names[i], source.source, probe);
        else {
            result.audio[i - 2] = wave(root / names[i]);
            (void)editor.execute(SetTrackPlayback{tracks[i], {true, false, 2000}});
        }
    }
    for (int cycle = 0; cycle < seconds; cycle += 12) {
        auto batch =
            editor.begin({{{"test"}, ActorKind::System}, "Reference cycle", editor.revision()});
        for (std::size_t i = 0; i < 6; ++i) {
            const std::vector<int> edges = i == 0   ? std::vector<int>{0, 4, 8, 12, 16, 20, 21, 24}
                                           : i == 1 ? std::vector<int>{0, 8, 16, 20, 21, 24}
                                                    : std::vector<int>{0, 8, 16, 22, 23, 24};
            for (std::size_t j = 0; j + 1 < edges.size(); ++j) {
                const int half = edges[j];
                if ((i < 2 && half == 20) || (i == 0 && half / 4 % 2 == 1) ||
                    (i >= 2 && half == 22))
                    continue;
                const RationalTime at{cycle * 2 + half, 2}, finish{cycle * 2 + edges[j + 1], 2};
                if (at >= RationalTime{seconds})
                    continue;
                (void)batch.execute(
                    InsertClip{tracks[i],
                               assets[i],
                               at,
                               {source_time(at), std::min(finish, RationalTime{seconds}) - at}});
            }
        }
        (void)editor.commit(std::move(batch));
    }
    result.project = deserialize(serialize(editor.snapshot()));
    return result;
}
int expected_video(RationalTime time) {
    const auto local = time - RationalTime{time.value() / time.rate() / 12 * 12};
    if (local >= RationalTime{10} && local < RationalTime{21, 2})
        return -1;
    return local.value() / local.rate() / 2 % 2 == 0 ? 0 : 1;
}
void audio_formats(const std::filesystem::path &root, const std::filesystem::path &probe) {
    std::atomic_bool stop = false;
    Editor editor("Audio precision");
    (void)editor.execute(media::probe(root / "multi-audio.mkv", probe).import_command());
    const auto asset = editor.snapshot().media.front();
    for (std::uint32_t stream = 0; stream < 2; ++stream) {
        const auto samples = wave(root / names[stream + 2]);
        decode::Decoder decoder(asset, stream, 960, 540, stop);
        for (const std::int64_t start : {0, 4801, 26399, 335999}) {
            decoder.seek({start, 48000});
            std::vector<float> actual(4800 * 2);
            decoder.mix(start, actual, 1.0F);
            for (std::size_t i = 0; i < actual.size(); ++i)
                check(actual[i] ==
                          static_cast<float>(samples[static_cast<std::size_t>(start) * 2 + i]) /
                              32768.0F,
                      "Coarse PCM timestamp or explicit stream mismatch");
        }
    }
    Editor mono("Mono");
    (void)mono.execute(media::probe(root / "mono-44100.wav", probe).import_command());
    decode::Decoder decoder(mono.snapshot().media.front(), 0, 960, 540, stop);
    decoder.seek({});
    std::vector<float> actual(48000 * 2);
    decoder.mix(0, actual, 1.0F);
    std::ifstream reference(root / "mono-reference.f32", std::ios::binary);
    std::vector<float> expected(actual.size());
    reference.read(reinterpret_cast<char *>(expected.data()),
                   static_cast<std::streamsize>(expected.size() * sizeof(float)));
    check(bool(reference), "Missing resample oracle");
    float error = 0;
    for (std::size_t i = 0; i < actual.size(); ++i)
        error = std::max(error, std::abs(actual[i] - expected[i]));
    check(error < 0.0001F, "Mono resampling / EOF flush mismatch");
    decoder.seek({913, 1000});
    std::vector<float> seek(480 * 2);
    decoder.mix(43824, seek, 1.0F);
    for (std::size_t i = 0; i < seek.size(); ++i)
        check(std::abs(seek[i] - actual[43824 * 2 + i]) < 0.0001F, "Resample seek changes output");
}
void audio_origins(const std::filesystem::path &root, const std::filesystem::path &probe) {
    std::vector<float> reference(4 * 48000 * 2);
    std::ifstream file(root / "origin-reference.f32", std::ios::binary);
    file.read(reinterpret_cast<char *>(reference.data()),
              static_cast<std::streamsize>(reference.size() * sizeof(float)));
    check(bool(file), "Missing origin audio oracle");
    std::atomic_bool stop = false;
    for (const auto *name : {"offset-common.mkv", "offset-audio.mkv", "offset-video.mkv",
                             "negative-origin.mkv", "negative-audio.mkv"}) {
        Editor editor("Audio origins");
        (void)editor.execute(media::probe(root / name, probe).import_command());
        const auto asset = editor.snapshot().media.front();
        const auto stream = select_stream(asset, TrackKind::Audio, {});
        check(stream.has_value(), "Missing origin audio stream");
        decode::Decoder decoder(asset, *stream, 960, 540, stop);
        const std::size_t delay = std::string(name) == "offset-audio.mkv" ? 24000 * 2 : 0;
        for (const std::int64_t at : {0, 23713, 24000, 24001, 43824, 191520}) {
            decoder.seek({at, 48000});
            std::vector<float> actual(at == 0 ? reference.size() : 480 * 2);
            decoder.mix(at, actual, 1.0F);
            for (std::size_t i = 0; i < actual.size(); ++i) {
                const auto index = static_cast<std::size_t>(at * 2) + i;
                const auto expected = index < delay ? 0.0F : reference[index - delay];
                check(std::abs(actual[i] - expected) < 0.000001F,
                      std::string(name) + " origin/delay/seek audio mismatch");
            }
        }
    }
}
void formats(const std::filesystem::path &root, const std::filesystem::path &probe) {
    std::atomic_bool stop = false;
    for (const auto *name : {"video.mp4", "vfr.mkv", "bframes.mp4", "offset-common.mkv",
                             "offset-audio.mkv", "offset-video.mkv", "negative-origin.mkv"}) {
        check(std::filesystem::exists(root / name), "Missing precision fixture");
        const auto imported = media::probe(root / name, probe);
        Editor e("Formats");
        const auto id = *e.execute(imported.import_command()).media;
        const auto asset = e.snapshot().media.front();
        (void)id;
        const auto index = media::index_frames(root / name, imported.source, probe);
        const auto stream = select_stream(asset, TrackKind::Video, {});
        if (!stream)
            continue;
        decode::Decoder decoder(asset, *stream, 960, 540, stop);
        for (const auto time :
             {RationalTime{0}, RationalTime{1001, 30000}, RationalTime{550, 1000},
              RationalTime{913, 1000}, RationalTime{137, 100}, RationalTime{1, 4}}) {
            decoder.seek(time);
            const auto actual = decoder.video(time);
            const auto expected = index.at(time);
            check(actual.pts.has_value() == expected.has_value(),
                  std::string(name) + " blank selection");
            if (expected)
                check(actual.pts == index.frames[*expected].position,
                      std::string(name) + " exact frame boundary mismatch");
        }
    }
}
} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        check(args.size() == 7, "worker corpus probe seconds report precision-corpus");
        const auto seconds = args[4].toInt();
        check(seconds >= 12 && seconds <= 600 && seconds % 12 == 0,
              "Duration must be 12..600, multiple of 12");
        formats(media::utf8_path(args[6].toStdString()), media::utf8_path(args[3].toStdString()));
        audio_origins(media::utf8_path(args[6].toStdString()),
                      media::utf8_path(args[3].toStdString()));
        audio_formats(media::utf8_path(args[2].toStdString()),
                      media::utf8_path(args[3].toStdString()));
        const auto ref = reference(media::utf8_path(args[2].toStdString()),
                                   media::utf8_path(args[3].toStdString()), seconds);
        desktop::SequenceTransport transport(args[1], false);
        transport.observePcm(true);
        QString failure;
        std::int64_t next_sample = 0;
        float sample_error = 0;
        int frame_errors = 0, black_errors = 0;
        QJsonObject ending;
        bool measuring = false;
        QElapsedTimer presentation;
        double first_late = 0, last_late = 0;
        bool first = true;
        std::deque<std::pair<qint64, double>> audio_delivery;
        double early_skew = 0, late_skew = 0;
        int early_count = 0, late_count = 0;
        const auto observe = QObject::connect(
            &transport, &desktop::SequenceTransport::observation, &app,
            [&](const QJsonObject &message) {
                if (message["type"] == "error")
                    failure = message["message"].toString();
                if (!measuring)
                    return;
                if (message["type"] == "video") {
                    const RationalTime time{message["timeline_value"].toString().toLongLong(),
                                            message["timeline_rate"].toString().toLongLong()};
                    const auto which = expected_video(time);
                    qint64 expected = -1;
                    if (which >= 0) {
                        const auto &index = ref.indexes[static_cast<std::size_t>(which)];
                        const auto frame = index.at(source_time(time));
                        if (frame)
                            expected = us(index.frames[*frame].position);
                    }
                    if (std::abs(message["pts_us"].toInteger() - expected) > 2) {
                        ++frame_errors;
                        std::cerr << "Frame mismatch timeline=" << time.value() << "/"
                                  << time.rate() << " expected=" << expected
                                  << " actual=" << message["pts_us"].toInteger()
                                  << " video=" << which << "\n";
                    }
                    if (which < 0) {
                        QImage image;
                        image.loadFromData(
                            QByteArray::fromBase64(message["image"].toString().toLatin1()), "JPG");
                        if (image.pixelColor(image.width() / 2, image.height() / 2) !=
                            QColor(Qt::black))
                            ++black_errors;
                    }
                    const auto late = static_cast<double>(presentation.nsecsElapsed()) / 1000000.0 -
                                      static_cast<double>(us(time)) / 1000.0;
                    if (first) {
                        first_late = late;
                        first = false;
                    }
                    last_late = late;
                    auto audio = audio_delivery.rend();
                    for (auto i = audio_delivery.rbegin(); i != audio_delivery.rend(); ++i)
                        if (i->first <= us(time)) {
                            audio = i;
                            break;
                        }
                    if (audio != audio_delivery.rend()) {
                        const auto skew =
                            static_cast<double>(presentation.nsecsElapsed()) / 1000000.0 -
                            audio->second - static_cast<double>(us(time) - audio->first) / 1000.0;
                        if (time >= RationalTime{1} && time < RationalTime{4}) {
                            early_skew += skew;
                            ++early_count;
                        }
                        if (time >= RationalTime{seconds - 4} && time < RationalTime{seconds - 1}) {
                            late_skew += skew;
                            ++late_count;
                        }
                    }
                } else if (message["type"] == "pcm") {
                    const auto sample = message["sample"].toInteger();
                    audio_delivery.emplace_back(sample * 1000000 / 48000,
                                                static_cast<double>(presentation.nsecsElapsed()) /
                                                    1000000.0);
                    if (audio_delivery.size() > 10)
                        audio_delivery.pop_front();
                    if (sample != next_sample)
                        failure = "Discontinuous PCM";
                    const auto bytes =
                        QByteArray::fromBase64(message["data"].toString().toLatin1());
                    check(bytes.size() % 8 == 0, "Malformed PCM");
                    for (qsizetype i = 0; i < bytes.size() / 4; ++i) {
                        float actual;
                        std::memcpy(&actual, bytes.constData() + i * 4, 4);
                        const auto t = sample + i / 2;
                        float expected = 0;
                        const auto local = t % (12 * 48000);
                        if (local < 11 * 48000 || local >= 23 * 24000) {
                            for (const auto &signal : ref.audio)
                                expected += 2.0F *
                                            static_cast<float>(signal[static_cast<std::size_t>(
                                                source_sample(t) * 2 + i % 2)]) /
                                            32768.0F;
                        }
                        sample_error = std::max(
                            sample_error, std::abs(actual - std::clamp(expected, -1.0F, 1.0F)));
                    }
                    next_sample = sample + bytes.size() / 8;
                } else if (message["type"] == "ended")
                    ending = message;
            });
        QElapsedTimer timer;
        timer.start();
        transport.open(ref.project, ref.sequence);
        wait([&] { return !transport.loading(); });
        check(failure.isEmpty() && transport.status() == "Paused",
              transport.status().toStdString());
        const auto ready = timer.elapsed();
        std::vector<double> seeks;
        for (const auto value : {550, 6913, 2370, 8550, 1110, 7370, 4100, 500, 9270, 10}) {
            timer.restart();
            transport.seek({value, 1000});
            wait([&] { return !transport.loading(); });
            check(transport.status() == "Paused", transport.status().toStdString());
            seeks.push_back(static_cast<double>(timer.elapsed()));
        }
        // Repeated replacement must discard every old worker output.
        for (int i = 0; i < 20; ++i)
            transport.seek({i * 11, 100});
        transport.seek({});
        wait([&] { return !transport.loading(); });
        measuring = true;
        presentation.start();
        transport.play();
        wait([&] { return !transport.playing(); }, seconds * 1000 + 10000);
        measuring = false;
        check(failure.isEmpty(), failure.toStdString());
        check(!ending.empty(), transport.status().toStdString());
        check(next_sample == seconds * 48000LL, "Truncated PCM output");
        timer.restart();
        transport.seek({1});
        transport.cancel();
        wait([&] { return transport.idle(); }, 1000);
        const auto cancel = timer.elapsed();
        const auto drift = std::abs(last_late - first_late);
        check(early_count > 10 && late_count > 10, "Insufficient A/V delivery pairs");
        const auto av_drift = std::abs(late_skew / late_count - early_skew / early_count);
        const auto dropped = ending["dropped"].toInteger(), frames = ending["frames"].toInteger();
        const bool passed = sample_error == 0 && frame_errors == 0 && black_errors == 0 &&
                            ready <= 3000 && percentile(seeks) <= 1000 && cancel <= 250 &&
                            ending["underruns"].toInteger() == 0 && dropped * 100 <= frames &&
                            drift <= 20 && av_drift <= 20 &&
                            ending["queue_peak_bytes"].toInteger() <= 64 * 1024 * 1024;
        QJsonObject result{{"backend", "sequence-worker"},
                           {"duration_seconds", seconds},
                           {"initial_ready_ms", ready},
                           {"seek_p95_ms", percentile(seeks)},
                           {"cancel_ms", cancel},
                           {"audio_max_sample_error", sample_error},
                           {"frame_errors", frame_errors},
                           {"black_errors", black_errors},
                           {"presentation_drift_ms", drift},
                           {"first_delivery_lateness_ms", first_late},
                           {"last_delivery_lateness_ms", last_late},
                           {"av_delivery_drift_ms", av_drift},
                           {"early_av_pairs", early_count},
                           {"late_av_pairs", late_count},
                           {"worker", ending},
                           {"passed", passed}};
        QFile report(args[5]);
        check(report.open(QIODevice::WriteOnly), "Cannot write report");
        report.write(QJsonDocument(result).toJson());
        std::cout << QJsonDocument(result).toJson().toStdString();
        QObject::disconnect(observe);
        return passed ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
