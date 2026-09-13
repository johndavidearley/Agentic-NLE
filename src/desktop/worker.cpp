#include <QAudioBuffer>
#include <QAudioBufferOutput>
#include <QAudioOutput>
#include <QBuffer>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QMediaPlayer>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    const auto args = app.arguments();
    // server, local file, start/end milliseconds, autoplay, audible
    if (args.size() != 7)
        return 2;
    bool start_ok = false, end_ok = false;
    const auto start = args[3].toLongLong(&start_ok), end = args[4].toLongLong(&end_ok);
    if (!start_ok || !end_ok || start < 0 || end <= start || end > 86400000 ||
        !QFileInfo(args[2]).isFile())
        return 2;
    QLocalSocket socket;
    socket.setReadBufferSize(4096);
    QMediaPlayer player;
    QVideoSink video;
    QAudioBufferOutput buffers;
    QAudioOutput output;
    player.setVideoSink(&video);
    player.setAudioBufferOutput(&buffers);
    if (args[6] == "1")
        player.setAudioOutput(&output);
    bool playing = args[5] == "1", loaded = false, ended = false;
    QElapsedTimer delivery, image_clock;
    delivery.start();
    const auto send = [&](QJsonObject message) {
        message.insert("wall_us", delivery.nsecsElapsed() / 1000);
        const auto encoded = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
        if (socket.bytesToWrite() + encoded.size() > 4 * 1024 * 1024) {
            app.exit(3);
            return;
        }
        socket.write(encoded);
    };
    const auto fail = [&](const QString &error) { send({{"type", "error"}, {"message", error}}); };
    QObject::connect(&socket, &QLocalSocket::disconnected, &app, &QCoreApplication::quit);
    QObject::connect(&socket, &QLocalSocket::connected, &app, [&] {
        player.setSource(QUrl::fromLocalFile(QFileInfo(args[2]).absoluteFilePath()));
    });
    QObject::connect(&player, &QMediaPlayer::errorOccurred, &app,
                     [&](QMediaPlayer::Error, const QString &message) { fail(message); });
    QObject::connect(&player, &QMediaPlayer::mediaStatusChanged, &app,
                     [&](QMediaPlayer::MediaStatus status) {
                         if (status == QMediaPlayer::LoadedMedia && !loaded) {
                             loaded = true;
                             if (!player.isSeekable()) {
                                 fail("Source does not support seeking.");
                                 return;
                             }
                             player.setPosition(start);
                             if (playing)
                                 player.play();
                             else
                                 player.pause();
                             send({{"type", "ready"},
                                   {"video", player.hasVideo()},
                                   {"audio", player.hasAudio()},
                                   {"duration_ms", player.duration()}});
                         } else if (status == QMediaPlayer::EndOfMedia && !ended) {
                             ended = true;
                             send({{"type", "ended"}});
                         }
                     });
    QObject::connect(&video, &QVideoSink::videoFrameChanged, &app, [&](const QVideoFrame &frame) {
        if (!frame.isValid() || !loaded || ended)
            return;
        const auto pts = frame.startTime(), finish = frame.endTime();
        // QMediaPlayer selects the seek frame. A VFR frame's nominal endTime can
        // precede the next presentation timestamp, so it is not a hold interval.
        if (pts < 0 || pts >= end * 1000)
            return;
        QJsonObject message{{"type", "video"}, {"pts_us", pts}, {"end_us", finish}};
        if (!image_clock.isValid() || image_clock.elapsed() >= 30 || !playing) {
            const auto image = frame.toImage().scaled(QSize(960, 540), Qt::KeepAspectRatio,
                                                      Qt::FastTransformation);
            if (image.isNull()) {
                fail("Cannot convert decoded video frame.");
                return;
            }
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            if (!image.save(&buffer, "JPG", 85) || bytes.size() > 1024 * 1024) {
                fail("Preview frame exceeds transfer limit.");
                return;
            }
            message.insert("image", QString::fromLatin1(bytes.toBase64()));
            image_clock.restart();
        }
        send(message);
    });
    QObject::connect(&buffers, &QAudioBufferOutput::audioBufferReceived, &app,
                     [&](const QAudioBuffer &buffer) {
                         if (loaded && !ended && buffer.isValid() && buffer.startTime() >= 0)
                             send({{"type", "audio"},
                                   {"pts_us", buffer.startTime()},
                                   {"duration_us", buffer.duration()}});
                     });
    QByteArray input;
    QObject::connect(&socket, &QLocalSocket::readyRead, &app, [&] {
        input += socket.readAll();
        if (input.size() > 4096) {
            app.exit(4);
            return;
        }
        for (qsizetype newline; (newline = input.indexOf('\n')) >= 0;) {
            const auto object = QJsonDocument::fromJson(input.left(newline)).object();
            input.remove(0, newline + 1);
            const auto command = object["command"].toString();
            if (command == "play") {
                playing = true;
                if (loaded)
                    player.play();
            } else if (command == "pause") {
                playing = false;
                if (loaded)
                    player.pause();
            } else {
                app.exit(4);
                return;
            }
        }
    });
    QTimer clock;
    clock.setTimerType(Qt::PreciseTimer);
    clock.setInterval(10);
    QObject::connect(&clock, &QTimer::timeout, &app, [&] {
        if (!loaded || ended)
            return;
        const auto position = player.position();
        if (position >= end) {
            player.pause();
            ended = true;
            send({{"type", "ended"}});
            return;
        }
        send({{"type", "position"}, {"ms", position}});
    });
    clock.start();
    socket.connectToServer(args[1]);
    QTimer::singleShot(10000, &app, [&] {
        if (!loaded)
            app.exit(5);
    });
    return app.exec();
}
