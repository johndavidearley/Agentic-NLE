#include "desktop/sequence_transport.hpp"
#include "media/probe.hpp"
#include <QAudioDevice>
#include <QAudioFormat>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QMediaDevices>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
using namespace nle;
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void wait(F done, int timeout = 5000) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    check(done(), "Wait timed out");
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        check(args.size() == 5, "worker probe corpus hanging-worker");
        QTemporaryDir temporary;
        check(temporary.isValid(), "Temporary directory");
        const auto file = temporary.filePath("source.wav");
        check(QFile::copy(args[3] + "/tone.wav", file), "Copy fixture");
        Editor editor("Supervision");
        const auto sequence = *editor.execute(CreateSequence{"Main"}).sequence;
        const auto track = *editor.execute(CreateTrack{sequence, TrackKind::Audio, "Audio"}).track;
        const auto asset = *editor
                                .execute(media::probe(media::utf8_path(file.toStdString()),
                                                      media::utf8_path(args[2].toStdString()))
                                             .import_command())
                                .media;
        (void)editor.execute(InsertClip{track, asset, {}, {{}, {2}}});
        const auto saved = editor.snapshot();
        desktop::SequenceTransport transport(args[1], false);
        transport.open(saved, sequence);
        wait([&] { return !transport.loading(); });
        check(transport.status() == "Paused", transport.status().toStdString());
        transport.play();
        wait([&] { return transport.position() > RationalTime{1, 10}; });
        int frames = 0;
        QObject::connect(&transport, &desktop::SequenceTransport::frameReady, &app,
                         [&](const QImage &image, qint64, qint64) {
                             if (!image.isNull())
                                 ++frames;
                         });
        QElapsedTimer timer;
        timer.start();
        transport.cancel();
        const int before = frames;
        wait([&] { return transport.idle(); }, 1000);
        check(timer.elapsed() <= 250, "Stop exceeded 250 ms");
        QElapsedTimer settle;
        settle.start();
        wait([&] { return settle.elapsed() > 100; });
        check(before == frames, "Old output after stop");
        transport.open(saved, sequence);
        wait([&] { return !transport.loading(); });
        const auto children = transport.findChildren<QProcess *>();
        check(children.size() == 1, "Expected one worker");
        children[0]->kill();
        wait([&] { return transport.idle(); });
        check(transport.status().contains("unexpectedly") ||
                  transport.status().contains("disconnected"),
              "Worker failure not surfaced");
        bool cancel_on_video = true;
        const auto connection =
            QObject::connect(&transport, &desktop::SequenceTransport::observation, &app,
                             [&](const QJsonObject &message) {
                                 if (cancel_on_video && message["type"] == "video") {
                                     cancel_on_video = false;
                                     transport.cancel();
                                 }
                             });
        const auto frames_before = frames;
        transport.open(saved, sequence);
        wait([&] { return !cancel_on_video && transport.idle(); });
        check(frames == frames_before, "Reentrant cancel admitted stale frame");
        QObject::disconnect(connection);
        transport.open(saved, sequence);
        wait([&] { return !transport.loading(); });
        {
            QFile output(file);
            check(output.open(QIODevice::Append), "Open mutable source");
            output.write("x");
        }
        transport.play();
        wait([&] { return !transport.playing(); });
        check(transport.status().contains("changed"), transport.status().toStdString());
        wait([&] { return transport.idle(); });
        check(editor.snapshot() == saved, "Playback changed document");
        desktop::SequenceTransport stalled(args[4], false);
        stalled.setLoadTimeout(150);
        stalled.open(saved, sequence);
        wait([&] { return !stalled.loading(); });
        check(stalled.status().contains("timed out"), "Missing load deadline");
        wait([&] { return stalled.idle(); }, 1000);
        // Exercise a real output device with silence when one is available, without producing a
        // tone.
        QAudioFormat format;
        format.setSampleRate(48000);
        format.setChannelCount(2);
        format.setSampleFormat(QAudioFormat::Float);
        const auto device = QMediaDevices::defaultAudioOutput();
        if (!device.isNull() && device.isFormatSupported(format)) {
            Editor silent("Sink");
            const auto seq = *silent.execute(CreateSequence{"Main"}).sequence;
            const auto t = *silent.execute(CreateTrack{seq, TrackKind::Audio, "Muted"}).track;
            const auto a =
                *silent
                     .execute(media::probe(media::utf8_path((args[3] + "/tone.wav").toStdString()),
                                           media::utf8_path(args[2].toStdString()))
                                  .import_command())
                     .media;
            (void)silent.execute(InsertClip{t, a, {}, {{}, {1}}});
            (void)silent.execute(SetTrackPlayback{t, {true, true, 1000}});
            desktop::SequenceTransport audible(args[1], true);
            audible.open(silent.snapshot(), seq);
            wait([&] { return !audible.loading(); });
            audible.play();
            wait([&] { return !audible.playing(); });
            check(audible.status() == "End of sequence", audible.status().toStdString());
            std::cout << "Audio device clock with silent PCM passed\n";
        } else
            std::cout << "No compatible audio device; device-clock check unavailable\n";
        std::cout
            << "Stop, stale generation, crash, source-change and load-timeout checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
