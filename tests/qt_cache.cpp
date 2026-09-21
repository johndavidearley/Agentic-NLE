#include "desktop/media_cache.hpp"
#include "media/probe.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
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
        QThread::msleep(2);
    }
    return f();
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        CHECK(args.size() == 5);
        QTemporaryDir dir;
        CHECK(dir.isValid());
        const auto copy = dir.filePath("source.wav");
        CHECK(QFile::copy(args[3] + "/audio-0.wav", copy));
        Editor e("Cache");
        const auto probe = media::utf8_path(args[2].toStdString());
        const auto audio =
            *e.execute(media::probe(media::utf8_path(copy.toStdString()), probe).import_command())
                 .media;
        const auto video =
            *e.execute(
                  media::probe(media::utf8_path((args[3] + "/video-a.mp4").toStdString()), probe)
                      .import_command())
                 .media;
        auto project = e.snapshot();
        MediaCache cache(args[1]);
        cache.synchronize(project);
        int ticks = 0;
        QTimer heartbeat;
        QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++ticks; });
        heartbeat.start(5);
        cache.request(audio, 0, true, {});
        cache.request(video, 0, false, {});
        CHECK(waitFor([&] { return cache.pending() == 0; }));
        const auto *wave = cache.request(audio, 0, true, {});
        CHECK(wave && wave->error.isEmpty() && wave->peaks.size() == 1000);
        const auto peaks = wave->peaks;
        // Independent PCM oracle, without the decoder or media cache.
        for (int bin = 0; bin < 1000; ++bin) {
            double peak = 0;
            for (int n = bin * 480; n < (bin + 1) * 480; ++n) {
                const auto angle = 2 * 3.14159265358979323846 * 240 * (n % 48000) / 48000;
                peak = std::max(peak, std::abs(std::round(8192 * std::sin(angle))) / 32768);
                peak = std::max(peak, std::abs(std::round(4096 * std::cos(angle))) / 32768);
            }
            CHECK(std::abs(peaks[static_cast<std::size_t>(bin)] - peak) < 0.00004);
        }
        const auto *thumb = cache.request(video, 0, false, {});
        CHECK(thumb && thumb->error.isEmpty() && thumb->image.size() == QSize(160, 90));
        CHECK(thumb->image.pixelColor(20, 20) != thumb->image.pixelColor(100, 50));
        CHECK(ticks > 0 && e.snapshot() == project);
        // Same-size source changes are detected by modification time after the TTL.
        QFile source(copy);
        CHECK(source.open(QIODevice::ReadWrite));
        CHECK(source.seek(44));
        CHECK(source.write(QByteArray::fromHex("ff7f")) == 2);
        source.close();
        const auto path = media::utf8_path(copy.toStdString());
        std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) +
                                                   std::chrono::seconds(2));
        QElapsedTimer ttl;
        ttl.start();
        CHECK(waitFor([&] { return ttl.elapsed() > 1100; }, 2000));
        cache.request(audio, 0, true, {});
        CHECK(waitFor([&] { return cache.pending() == 0; }));
        wave = cache.request(audio, 0, true, {});
        CHECK(wave && wave->error.isEmpty() && wave->peaks[0] > 0.99F);
        // Relinking drops obsolete entries without changing source timing.
        auto moved = project;
        moved.media[0].locations[0].uri += ".missing";
        cache.synchronize(moved);
        CHECK(cache.entries() == 0);
        for (int batch = 0; batch < 10; ++batch) {
            for (int n = 0; n < 32; ++n)
                cache.request(audio, 0, true, {batch * 32 + n, 100});
            CHECK(cache.pending() <= MediaCache::queueLimit + 2);
            CHECK(waitFor([&] { return cache.pending() == 0; }));
            CHECK(cache.entries() <= MediaCache::entryLimit &&
                  cache.residentBytes() <= MediaCache::byteLimit);
        }
        CHECK(cache.entries() == MediaCache::entryLimit);
        CHECK(!cache.request(audio, 0, true, {319, 100})->error.isEmpty());
        // A stuck child expires in the supervisor while the UI remains responsive.
        MediaCache hanging(args[4]);
        hanging.synchronize(project);
        const int beforeTicks = ticks;
        QElapsedTimer deadline;
        deadline.start();
        hanging.request(audio, 0, true, {});
        CHECK(waitFor([&] { return hanging.pending() == 0; }, 11000));
        CHECK(deadline.elapsed() < 10000 && ticks - beforeTicks > 50);
        CHECK(!hanging.request(audio, 0, true, {})->error.isEmpty());
        hanging.request(audio, 0, true, {1});
        hanging.synchronize(moved);
        hanging.request(audio, 0, true, {2});
        CHECK(waitFor([&] { return hanging.pending() == 0; }, 1500));
        CHECK(hanging.entries() == 1); // Only the new-generation missing-source response survives.
        CHECK(e.snapshot() == project);
        std::cout << "Thumbnail, PCM waveform oracle, source identity, LRU limits and responsive "
                     "cancellation passed\n";
        return 0;
    } catch (const std::exception &x) {
        std::cerr << x.what() << '\n';
        return 1;
    }
}
