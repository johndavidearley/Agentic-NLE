#include "desktop/transport.hpp"
#include "media/frame_index.hpp"
#include "media/probe.hpp"
#include "multitrack_decode.hpp"
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <numeric>
#include <thread>
using namespace nle;
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
using nle::prototype::Decoder;
const std::array<std::string, 6> names{"video-a.mp4", "video-b.mkv", "audio-0.wav",
                                       "audio-1.wav", "audio-2.wav", "audio-3.wav"};
double elapsed(Clock::time_point began) {
    return std::chrono::duration<double, std::milli>(Clock::now() - began).count();
}
double percentile(std::vector<double> values, double fraction) {
    if (values.empty())
        return 0;
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1))];
}
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void wait_for(F predicate, int timeout = 35000) {
    const auto began = Clock::now();
    while (!predicate() && elapsed(began) < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    require(predicate(), "Prototype wait timed out");
}
std::int64_t source_us(std::int64_t timeline_us) {
    constexpr std::array<std::int64_t, 3> starts{0, 6000000, 2000000};
    return starts[static_cast<std::size_t>(timeline_us / 4000000 % 3)] + timeline_us % 4000000;
}
std::vector<std::int16_t> wav(const std::filesystem::path &file) {
    std::ifstream input(file, std::ios::binary);
    input.seekg(44);
    std::vector<std::int16_t> samples(12 * 48000 * 2);
    input.read(reinterpret_cast<char *>(samples.data()),
               static_cast<std::streamsize>(samples.size() * 2));
    require(static_cast<bool>(input), "Cannot read reference WAV");
    return samples;
}
struct Chunk {
    int tick = 0;
    std::array<float, 960> audio{};
    nle::prototype::Video video;
    std::int64_t expected_pts = -1;
    std::size_t bytes() const { return sizeof(Chunk) + video.rgb.size(); }
};
QJsonObject native_run(const std::filesystem::path &root, const std::filesystem::path &probe,
                       int seconds) {
    std::array<media::FrameIndex, 2> indexes;
    for (std::size_t i = 0; i < indexes.size(); ++i) {
        const auto asset = media::probe(root / names[i], probe);
        indexes[i] = media::index_frames(root / names[i], asset.source, probe);
    }
    std::array<std::vector<std::int16_t>, 4> reference;
    for (std::size_t i = 0; i < reference.size(); ++i)
        reference[i] = wav(root / names[i + 2]);
    std::atomic_bool stop = false;
    std::vector<std::unique_ptr<Decoder>> decoders;
    const auto began = Clock::now();
    for (std::size_t i = 0; i < names.size(); ++i)
        decoders.push_back(
            std::make_unique<Decoder>(media::path_utf8(root / names[i]), i < 2, stop));
    std::mutex mutex;
    std::condition_variable available;
    std::deque<Chunk> queue;
    std::size_t queue_bytes = 0, peak_queue = 0;
    bool done = false;
    std::exception_ptr error;
    std::vector<double> cut_times;
    std::jthread producer([&] {
        try {
            for (int tick = 0; tick < seconds * 100 && !stop.load(); ++tick) {
                const auto position = source_us(static_cast<std::int64_t>(tick) * 10000);
                if (tick % 400 == 0) {
                    const auto start = Clock::now();
                    for (auto &decoder : decoders)
                        decoder->seek(position);
                    cut_times.push_back(elapsed(start));
                }
                Chunk chunk;
                chunk.tick = tick;
                const bool audio_gap = tick % 1200 >= 1100 && tick % 1200 < 1150;
                if (!audio_gap) {
                    for (std::size_t i = 2; i < decoders.size(); ++i)
                        decoders[i]->mix(position * 48000 / 1000000, chunk.audio, 2.0F);
                    for (auto &sample : chunk.audio)
                        sample = std::clamp(sample, -1.0F, 1.0F);
                }
                // Output events at the first 10 ms audio block crossing each 30000/1001 grid point.
                const bool frame = tick == 0 || tick * 10000LL * 30000 / 1001000000 !=
                                                    (tick - 1) * 10000LL * 30000 / 1001000000;
                if (frame) {
                    const bool black = tick % 1200 >= 1000 && tick % 1200 < 1050;
                    const auto top = static_cast<std::size_t>(tick / 200 % 2);
                    // Decode both active tracks to exercise the full prototype workload.
                    std::array<nle::prototype::Video, 2> video{decoders[0]->video(position),
                                                               decoders[1]->video(position)};
                    if (!black) {
                        chunk.video = std::move(video[top]);
                        const auto selected = indexes[top].at(RationalTime{position, 1000000});
                        require(selected.has_value(), "Reference frame missing");
                        const auto pts = indexes[top].frames[*selected].position;
                        chunk.expected_pts = pts.value() * 1000000 / pts.rate();
                    } else {
                        chunk.video = {-1, 960, 540, std::vector<std::uint8_t>(960 * 540 * 3)};
                    }
                }
                std::unique_lock lock(mutex);
                available.wait(lock, [&] { return stop.load() || queue.size() < 32; });
                if (stop.load())
                    break;
                queue_bytes += chunk.bytes();
                peak_queue = std::max(peak_queue, queue_bytes);
                queue.push_back(std::move(chunk));
                available.notify_all();
            }
        } catch (...) {
            std::lock_guard lock(mutex);
            error = std::current_exception();
        }
        {
            std::lock_guard lock(mutex);
            done = true;
        }
        available.notify_all();
    });
    struct Cleanup {
        std::atomic_bool &stop;
        std::condition_variable &available;
        std::jthread &producer;
        ~Cleanup() {
            stop = true;
            available.notify_all();
            if (producer.joinable())
                producer.join();
        }
    } cleanup{stop, available, producer};
    {
        std::unique_lock lock(mutex);
        available.wait(lock, [&] { return queue.size() >= 24 || done; });
    }
    const auto ready_ms = elapsed(began);
    const auto play = Clock::now();
    int underruns = 0, dropped = 0, frames = 0, reference_errors = 0;
    std::int64_t maximum_pts_error = 0;
    float audio_error = 0;
    std::vector<double> late;
    for (int tick = 0; tick < seconds * 100; ++tick) {
        std::this_thread::sleep_until(play + std::chrono::milliseconds(tick * 10));
        Chunk chunk;
        {
            std::unique_lock lock(mutex);
            if (queue.empty() && !done)
                ++underruns;
            available.wait(lock, [&] { return !queue.empty() || done; });
            if (queue.empty())
                break;
            queue_bytes -= queue.front().bytes();
            chunk = std::move(queue.front());
            queue.pop_front();
            available.notify_all();
        }
        require(chunk.tick == tick, "Discontinuous audio sample clock");
        const auto lateness = elapsed(play) - tick * 10;
        late.push_back(lateness);
        const auto first = source_us(static_cast<std::int64_t>(tick) * 10000) * 48000 / 1000000;
        const bool silence = tick % 1200 >= 1100 && tick % 1200 < 1150;
        for (std::size_t sample = 0; sample < chunk.audio.size(); ++sample) {
            float expected = 0;
            if (!silence)
                for (const auto &signal : reference)
                    expected +=
                        2.0F *
                        static_cast<float>(signal[static_cast<std::size_t>(first) * 2 + sample]) /
                        32768.0F;
            expected = std::clamp(expected, -1.0F, 1.0F);
            audio_error = std::max(audio_error, std::abs(expected - chunk.audio[sample]));
        }
        if (!chunk.video.rgb.empty()) {
            ++frames;
            if (lateness > 1001.0 / 30)
                ++dropped;
            maximum_pts_error =
                std::max(maximum_pts_error, std::abs(chunk.video.pts_us - chunk.expected_pts));
            if (chunk.expected_pts < 0 && std::any_of(
                                              chunk.video.rgb.begin(), chunk.video.rgb.end(),
                                              [](auto value) { return value != 0; }))
                ++reference_errors;
        }
    }
    stop = true;
    available.notify_all();
    producer.join();
    if (error)
        std::rethrow_exception(error);
    // Explicit seek readiness includes first decoded video and PCM from all six sources.
    stop = false;
    std::vector<double> seeks;
    for (const auto target :
         {550000, 6913000, 2370000, 8550000, 1110000, 7370000, 4100000, 500000, 9270000, 10000}) {
        const auto start = Clock::now();
        for (std::size_t i = 0; i < decoders.size(); ++i) {
            decoders[i]->seek(target);
            if (i < 2)
                (void)decoders[i]->video(target);
            else {
                std::array<float, 960> audio{};
                decoders[i]->mix(target * 48000LL / 1000000, audio, 1);
            }
        }
        seeks.push_back(elapsed(start));
    }
    std::atomic_bool started = false;
    std::jthread cancelling([&] {
        started = true;
        try {
            while (!stop.load()) {
                decoders[1]->seek(7000000);
                (void)decoders[1]->video(7913000);
            }
        } catch (const std::exception &) {
            if (!stop.load())
                std::terminate();
        }
    });
    while (!started.load())
        std::this_thread::yield();
    std::this_thread::sleep_for(2ms);
    const auto cancel_start = Clock::now();
    stop = true;
    cancelling.join();
    const auto cancel_ms = elapsed(cancel_start);
    const auto drift = late.empty() ? 0 : std::abs(late.back() - late.front());
    const auto dropped_percent = frames ? 100.0 * dropped / frames : 100.0;
    const bool passed = ready_ms <= 3000 && percentile(seeks, .95) <= 1000 && cancel_ms <= 250 &&
                        underruns == 0 && dropped_percent <= 1 && audio_error <= 0.000001F &&
                        maximum_pts_error <= 2 && reference_errors == 0 && drift <= 20 &&
                        peak_queue <= 64 * 1024 * 1024;
    return {{"backend", "direct-ffmpeg-prototype"},
            {"ffmpeg", QString::fromStdString(Decoder::version())},
            {"duration_seconds", seconds},
            {"initial_ready_ms", ready_ms},
            {"seek_p95_ms", percentile(seeks, .95)},
            {"cancel_ms", cancel_ms},
            {"underruns", underruns},
            {"late_video_frames", dropped},
            {"video_frames", frames},
            {"late_video_percent", dropped_percent},
            {"audio_max_sample_error", audio_error},
            {"max_pts_error_us", static_cast<qint64>(maximum_pts_error)},
            {"black_frame_errors", reference_errors},
            {"clock_lateness_p99_ms", percentile(late, .99)},
            {"clock_drift_ms", drift},
            {"peak_decoded_queue_bytes", static_cast<qint64>(peak_queue)},
            {"cut_seek_p95_ms", percentile(cut_times, .95)},
            {"passed_in_process_targets", passed}};
}
QJsonObject format_checks(const std::filesystem::path &root, const std::filesystem::path &probe) {
    std::atomic_bool stop = false;
    QJsonArray results;
    for (const auto *name : {"vfr.mkv", "bframes.mp4", "offset-common.mkv", "offset-audio.mkv",
                             "offset-video.mkv", "negative-origin.mkv"}) {
        const auto file = root / name;
        const auto source = media::probe(file, probe).source;
        const auto index = media::index_frames(file, source, probe);
        Decoder decoder(media::path_utf8(file), true, stop);
        std::int64_t error = 0;
        for (const auto target : {0, 550000, 913000, 1370000, 250000, 1750000, 10000}) {
            decoder.seek(target);
            const auto actual = decoder.video(target);
            const auto selected = index.at(RationalTime{target, 1000000});
            std::int64_t expected = -1;
            if (selected) {
                const auto time = index.frames[*selected].position;
                expected = time.value() * 1000000 / time.rate();
            }
            const auto delta = std::abs(actual.pts_us - expected);
            if (delta > 2) {
                std::cerr << name << " target=" << target << " expected=" << expected
                          << " actual=" << actual.pts_us << '\n';
                throw std::runtime_error("Native source-origin/frame reference mismatch");
            }
            error = std::max(error, delta);
        }
        results.push_back(
            QJsonObject{{"file", name}, {"max_pts_error_us", static_cast<qint64>(error)}});
    }
    for (const auto *name : {"offset-audio.mkv", "negative-audio.mkv"}) {
        Decoder decoder(media::path_utf8(root / name), false, stop);
        decoder.seek(250000);
        std::array<float, 960> audio{};
        decoder.mix(12000, audio, 1);
        const bool silent =
            std::all_of(audio.begin(), audio.end(), [](float value) { return value == 0; });
        require(silent == (std::string(name) == "offset-audio.mkv"), "Audio origin/gap mismatch");
    }
    return {{"backend", "direct-ffmpeg-format-checks"},
            {"passed", true},
            {"video", results},
            {"audio_origin_checks", 2}};
}
QJsonObject qt_run(const std::filesystem::path &root, const QString &probe, const QString &worker,
                   int seconds) {
    std::vector<playback::Plan> plans;
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto result = media::probe(root / names[i], media::utf8_path(probe.toStdString()));
        Editor editor("Qt comparison");
        const auto asset = *editor.execute(result.import_command()).media;
        const auto seq = *editor.execute(CreateSequence{"Main"}).sequence;
        const auto track =
            *editor.execute(CreateTrack{seq, i < 2 ? TrackKind::Video : TrackKind::Audio, "Track"})
                 .track;
        for (int position = 0; position < seconds; position += 4)
            (void)editor.execute(InsertClip{track,
                                            asset,
                                            RationalTime{position},
                                            {RationalTime{source_us(position * 1000000LL), 1000000},
                                             RationalTime{std::min(4, seconds - position)}}});
        plans.push_back(playback::make_plan(editor.snapshot(), seq));
    }
    std::vector<std::unique_ptr<desktop::Transport>> transports;
    const auto began = Clock::now();
    for (std::size_t i = 0; i < names.size(); ++i) {
        auto transport = std::make_unique<desktop::Transport>(worker, false, nullptr, probe);
        transport->open(plans[i]);
        transports.push_back(std::move(transport));
    }
    const auto all_ready = [&] {
        return std::all_of(transports.begin(), transports.end(),
                           [](const auto &t) { return !t->loading(); });
    };
    const auto all_idle = [&] {
        return std::all_of(transports.begin(), transports.end(),
                           [](const auto &t) { return t->idle(); });
    };
    wait_for(all_ready);
    const auto ready_ms = elapsed(began);
    for (const auto &t : transports)
        require(t->status() == "Paused", t->status().toStdString().c_str());
    std::vector<double> seeks;
    for (const auto target :
         {550000, 6913000, 2370000, 8550000, 1110000, 7370000, 4100000, 500000, 9270000, 10000}) {
        const auto start = Clock::now();
        for (const auto &t : transports)
            t->seek(RationalTime{target, 1000000});
        wait_for(all_ready);
        for (const auto &t : transports)
            require(t->status() == "Paused", t->status().toStdString().c_str());
        seeks.push_back(elapsed(start));
    }
    for (const auto &t : transports)
        t->seek({});
    wait_for(all_ready);
    for (const auto &t : transports)
        t->play();
    const auto play = Clock::now();
    std::int64_t max_spread_ms = 0;
    int observed_loading = 0;
    while (elapsed(play) < seconds * 1000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        std::int64_t low = std::numeric_limits<std::int64_t>::max(), high = 0;
        for (const auto &t : transports) {
            const auto position = playback::milliseconds(t->position());
            low = std::min(low, position);
            high = std::max(high, position);
            if (t->loading())
                ++observed_loading;
        }
        max_spread_ms = std::max(max_spread_ms, high - low);
        QThread::msleep(5);
    }
    const auto cancel_start = Clock::now();
    for (const auto &t : transports)
        t->cancel();
    wait_for(all_idle);
    return {{"backend", "existing-six-qt-workers"},
            {"duration_seconds", seconds},
            {"initial_ready_ms", ready_ms},
            {"seek_p95_ms", percentile(seeks, .95)},
            {"cancel_ms", elapsed(cancel_start)},
            {"max_track_clock_spread_ms", static_cast<qint64>(max_spread_ms)},
            {"loading_observations", observed_loading},
            {"joint_sample_mixer", false},
            {"note", "Existing worker protocol exports audio timing only; independent players do "
                     "not expose a shared sample clock or PCM mixer."}};
}
} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        require(args.size() == 7, "backend corpus ffprobe worker seconds report");
        const auto root = media::utf8_path(args[2].toStdString());
        const int seconds = args[5].toInt();
        require(seconds >= 12 && seconds <= 600, "Prototype duration must be 12..600 seconds");
        const auto report = args[1] == "checks"
                                ? format_checks(root, media::utf8_path(args[3].toStdString()))
                            : args[1] == "native"
                                ? native_run(root, media::utf8_path(args[3].toStdString()), seconds)
                                : qt_run(root, args[3], args[4], seconds);
        QFile output(args[6]);
        require(output.open(QIODevice::WriteOnly), "Cannot write report");
        output.write(QJsonDocument(report).toJson());
        std::cout << QJsonDocument(report).toJson().toStdString();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
