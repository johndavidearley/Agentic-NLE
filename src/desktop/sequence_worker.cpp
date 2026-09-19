#include "decode/renderer.hpp"
#include "project/persistence.hpp"
#include <QAudioDevice>
#include <QAudioSink>
#include <QBuffer>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QMediaDevices>
#include <QTimer>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
using namespace nle;
namespace {
qint64 us(RationalTime t) {
    if (t.value() > std::numeric_limits<qint64>::max() / 1000000)
        throw DomainError("Preview timestamp exceeds conversion limit");
    return t.value() * 1000000 / t.rate();
}
struct Picture {
    RationalTime position;
    QByteArray message;
};
struct Chunk {
    std::int64_t sample;
    std::vector<float> audio;
    std::vector<Picture> pictures;
    std::size_t bytes() const {
        auto total = audio.size() * sizeof(float);
        for (const auto &p : pictures)
            total += static_cast<std::size_t>(p.message.size());
        return total;
    }
};
struct Queue {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Chunk> chunks;
    std::optional<Picture> poster;
    std::atomic_bool stop = false;
    std::string error;
    bool done = false;
    std::size_t bytes = 0, peak = 0;
};
} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 2)
        return 2;
    QLocalSocket socket;
    socket.setReadBufferSize(20 * 1024 * 1024);
    Queue queue;
    std::thread producer;
    struct Cleanup {
        Queue &q;
        std::thread &t;
        ~Cleanup() {
            q.stop = true;
            q.changed.notify_all();
            if (t.joinable())
                t.join();
        }
    } cleanup{queue, producer};
    QByteArray input;
    bool initialized = false, ready = false, playing = false, audible = false, observe = false,
         ended = false;
    RationalTime start, duration, frame_duration;
    std::int64_t first_sample = 0, submitted = 0;
    QElapsedTimer clock, wall;
    wall.start();
    std::unique_ptr<QAudioSink> sink;
    QIODevice *output = nullptr;
    std::deque<Picture> pictures;
    std::optional<Chunk> current;
    std::size_t offset = 0;
    std::uint64_t underruns = 0, dropped = 0, frames = 0;
    qint64 last_position = -1, last_tick = -1, max_tick_gap_us = 0;
    std::atomic<qint64> max_encode_us = 0;
    QJsonArray late_frames;
    const auto write = [&](const QByteArray &bytes) {
        if (socket.bytesToWrite() + bytes.size() > 4 * 1024 * 1024) {
            app.exit(3);
            return;
        }
        socket.write(bytes);
    };
    const auto send = [&](QJsonObject msg) {
        write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n');
    };
    const auto fail = [&](const QString &text) {
        if (ended)
            return;
        ended = true;
        playing = false;
        queue.stop = true;
        queue.changed.notify_all();
        if (sink)
            sink->reset();
        send({{"type", "error"}, {"message", text}});
    };
    // Encode on the producer before enqueueing; the presentation clock only sends
    // prepared bytes. A slow codec/plugin cannot hold up audio feeding or timers.
    const auto prepare = [&](const decode::Picture &p) {
        QElapsedTimer encoding;
        encoding.start();
        const auto &v = p.video;
        QImage image(v.rgb.data(), v.width, v.height, v.width * 3, QImage::Format_RGB888);
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "JPG", 85))
            throw DomainError("Cannot encode preview image");
        QJsonObject message{{"type", "video"},
                            {"timeline_us", us(p.position)},
                            {"timeline_value", QString::number(p.position.value())},
                            {"timeline_rate", QString::number(p.position.rate())},
                            {"clip", QString::number(p.clip.value)},
                            {"pts_us", v.pts ? us(*v.pts) : -1},
                            {"end_us", v.end ? us(*v.end) : -1},
                            {"image", QString::fromLatin1(data.toBase64())}};
        Picture prepared{p.position, QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n'};
        max_encode_us = std::max(max_encode_us.load(), encoding.nsecsElapsed() / 1000);
        return prepared;
    };
    const auto picture = [&](const Picture &p) {
        ++frames;
        if (socket.bytesToWrite() > 2 * 1024 * 1024) {
            ++dropped;
            return;
        }
        write(p.message);
    };
    const auto begin = [&] {
        if (!ready || playing || ended)
            return;
        if (audible) {
            QAudioFormat format;
            format.setSampleRate(48000);
            format.setChannelCount(2);
            format.setSampleFormat(QAudioFormat::Float);
            const auto device = QMediaDevices::defaultAudioOutput();
            if (device.isNull() || !device.isFormatSupported(format)) {
                fail("The audio device does not support stereo float at 48 kHz");
                return;
            }
            sink = std::make_unique<QAudioSink>(device, format);
            sink->setBufferSize(48000 * 2 * 4 / 25);
            output = sink->start();
            if (!output) {
                fail("Cannot start audio output");
                return;
            }
            if (sink->bufferSize() > 48000 * 2 * 4 / 4) {
                fail("Audio device requires more than the supported 250 ms buffer");
                return;
            }
        }
        clock.start();
        playing = true;
    };
    QObject::connect(&socket, &QLocalSocket::disconnected, &app, &QCoreApplication::quit);
    QObject::connect(&socket, &QLocalSocket::errorOccurred, &app,
                     [&](QLocalSocket::LocalSocketError) { app.exit(4); });
    QObject::connect(&socket, &QLocalSocket::readyRead, &app, [&] {
        input += socket.readAll();
        if (input.size() > 20 * 1024 * 1024) {
            fail("Preview request exceeds limit");
            return;
        }
        for (qsizetype newline; (newline = input.indexOf('\n')) >= 0;) {
            QJsonParseError error;
            auto document = QJsonDocument::fromJson(input.left(newline), &error);
            input.remove(0, newline + 1);
            if (error.error != QJsonParseError::NoError || !document.isObject()) {
                fail("Invalid preview request");
                return;
            }
            const auto msg = document.object();
            if (msg["command"] == "play") {
                begin();
                continue;
            }
            if (msg["command"] != "open" || initialized) {
                fail("Invalid playback command");
                return;
            }
            try {
                const auto project = deserialize(msg["project"].toString().toStdString());
                auto plan = playback::make_sequence_plan(
                    project, SequenceId{msg["sequence"].toString().toULongLong()});
                start = {msg["value"].toString().toLongLong(), msg["rate"].toString().toLongLong()};
                (void)plan.evaluate(start);
                duration = plan.duration;
                frame_duration = plan.frame_duration;
                audible = msg["audible"].toBool();
                observe = msg["observe_pcm"].toBool();
                initialized = true;
                first_sample = decode::sample_ceil(start);
                submitted = first_sample;
                producer = std::thread([&, plan = std::move(plan)]() mutable {
                    try {
                        decode::Renderer renderer(std::move(plan), start, queue.stop);
                        auto poster = prepare(renderer.poster(start));
                        {
                            std::lock_guard lock(queue.mutex);
                            queue.poster = std::move(poster);
                        }
                        for (;;) {
                            {
                                std::unique_lock lock(queue.mutex);
                                queue.changed.wait(
                                    lock, [&] { return queue.stop || queue.chunks.size() < 32; });
                                if (queue.stop)
                                    return;
                            }
                            auto decoded = renderer.next();
                            if (decoded.end) {
                                std::lock_guard lock(queue.mutex);
                                queue.done = true;
                                return;
                            }
                            Chunk chunk{decoded.sample, std::move(decoded.audio), {}};
                            for (const auto &p : decoded.pictures)
                                chunk.pictures.push_back(prepare(p));
                            std::lock_guard lock(queue.mutex);
                            queue.bytes += chunk.bytes();
                            queue.peak = std::max(queue.peak, queue.bytes);
                            if (queue.bytes > 64 * 1024 * 1024)
                                throw DomainError("Decoded queue exceeds 64 MiB");
                            queue.chunks.push_back(std::move(chunk));
                        }
                    } catch (const std::exception &e) {
                        std::lock_guard lock(queue.mutex);
                        queue.error = e.what();
                    }
                });
            } catch (const std::exception &e) {
                fail(QString::fromUtf8(e.what()));
            }
        }
    });
    QTimer timer;
    timer.setInterval(2);
    timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        if (!initialized || ended)
            return;
        {
            std::unique_lock lock(queue.mutex);
            if (!queue.error.empty()) {
                const auto error = queue.error;
                lock.unlock();
                fail(QString::fromStdString(error));
                return;
            }
            if (!ready) {
                if ((queue.chunks.size() < 24 && !queue.done) || !queue.poster)
                    return;
                auto poster = std::move(*queue.poster);
                queue.poster.reset();
                ready = true;
                lock.unlock();
                picture(poster);
                send({{"type", "ready"}, {"initial_ready_ms", wall.elapsed()}});
                return;
            }
        }
        if (!playing)
            return;
        const auto tick = clock.nsecsElapsed() / 1000;
        if (last_tick >= 0)
            max_tick_gap_us = std::max(max_tick_gap_us, tick - last_tick);
        last_tick = tick;
        if (sink && sink->error() != QAudio::NoError && sink->error() != QAudio::UnderrunError) {
            fail("Audio output failed");
            return;
        }
        if (sink && sink->error() == QAudio::UnderrunError && clock.elapsed() > 50 &&
            submitted < decode::sample_ceil(duration)) {
            ++underruns;
            fail("Audio device underrun; playback stopped");
            return;
        }
        const auto elapsed = sink ? sink->processedUSecs() : clock.nsecsElapsed() / 1000;
        const auto now = std::min<qint64>(first_sample + elapsed * 48000 / 1000000,
                                          decode::sample_ceil(duration));
        // Submit only one device buffer ahead. In silent mode, consume on the same sample clock.
        for (;;) {
            if (!current) {
                std::lock_guard lock(queue.mutex);
                if (queue.chunks.empty())
                    break;
                if (!sink && queue.chunks.front().sample > now)
                    break;
                current = std::move(queue.chunks.front());
                queue.bytes -= current->bytes();
                queue.chunks.pop_front();
                queue.changed.notify_one();
                offset = 0;
                for (auto &p : current->pictures)
                    pictures.push_back(std::move(p));
                current->pictures.clear();
                if (observe)
                    send({{"type", "pcm"},
                          {"sample", static_cast<qint64>(current->sample)},
                          {"data",
                           QString::fromLatin1(
                               QByteArray(
                                   reinterpret_cast<const char *>(current->audio.data()),
                                   static_cast<qsizetype>(current->audio.size() * sizeof(float)))
                                   .toBase64())}});
            }
            const auto size = current->audio.size() * sizeof(float);
            if (sink) {
                const auto free = sink->bytesFree();
                if (free <= 0)
                    break;
                const auto count = std::min<qint64>(free, static_cast<qint64>(size - offset));
                const auto written = output->write(
                    reinterpret_cast<const char *>(current->audio.data()) + offset, count);
                if (written < 0) {
                    fail("Cannot write audio output");
                    return;
                }
                if (written == 0)
                    break;
                offset += static_cast<std::size_t>(written);
                if (offset < size)
                    break;
            }
            submitted = current->sample + static_cast<std::int64_t>(current->audio.size() / 2);
            current.reset();
        }
        if (submitted < now && now < decode::sample_ceil(duration)) {
            ++underruns;
            fail("Playback underrun; decoded data missed the sample clock");
            return;
        }
        while (!pictures.empty() && us(pictures.front().position) <= now * 1000000 / 48000) {
            auto p = std::move(pictures.front());
            pictures.pop_front();
            if (now * 1000000 / 48000 - us(p.position) > us(frame_duration)) {
                ++dropped;
                if (late_frames.size() < 32)
                    late_frames.append(
                        QJsonObject{{"timeline_us", us(p.position)},
                                    {"late_us", now * 1000000 / 48000 - us(p.position)}});
            }
            picture(p);
        }
        if (now * 1000000 / 48000 - last_position >= 20000) {
            last_position = now * 1000000 / 48000;
            send({{"type", "position"}, {"sample", now}});
        }
        if (now >= decode::sample_ceil(duration)) {
            ended = true;
            playing = false;
            if (sink)
                sink->reset();
            std::size_t peak;
            {
                std::lock_guard lock(queue.mutex);
                peak = queue.peak;
            }
            send({{"type", "ended"},
                  {"underruns", static_cast<qint64>(underruns)},
                  {"dropped", static_cast<qint64>(dropped)},
                  {"frames", static_cast<qint64>(frames)},
                  {"max_tick_gap_us", max_tick_gap_us},
                  {"max_encode_us", max_encode_us.load()},
                  {"late_frames", late_frames},
                  {"queue_peak_bytes", static_cast<qint64>(peak)}});
        }
    });
    timer.start();
    socket.connectToServer(args[1]);
    return app.exec();
}
