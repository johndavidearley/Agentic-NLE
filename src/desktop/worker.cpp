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
#include <algorithm>
#include <cmath>

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    const auto args = app.arguments();
    // server, file, requested/end ms, autoplay, audible, poster-seek ms,
    // expected poster PTS in source microseconds (-1 means blank), player clock offset us,
    // load timeout in ms.
    if (args.size() != 11)
        return 2;
    bool valid = true;
    const auto number = [&](int index) {
        bool ok = false;
        const auto value = args[index].toLongLong(&ok);
        valid = valid && ok;
        return value;
    };
    const auto start = number(3), end = number(4), seek = number(7), expected = number(8),
               shift = number(9), load_timeout = number(10);
    if (!valid || start < 0 || end <= start || end > 86400000 || seek < 0 || seek > 86400001 ||
        expected < -1 || expected > 86400000000LL || shift < -86400000000LL ||
        shift > 86400000000LL || load_timeout < 0 || load_timeout > 86400000 ||
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
    bool playing = args[5] == "1", loaded = false, ended = false, ready = false;
    qint64 resume = start;
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
    const auto announce = [&] {
        if (ready)
            return;
        ready = true;
        send({{"type", "ready"},
              {"video", player.hasVideo()},
              {"audio", player.hasAudio()},
              {"duration_ms", player.duration()}});
    };
    const auto player_position = [&](qint64 source_ms) {
        return std::max<qint64>(0, source_ms + shift / 1000);
    };
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
                             player.setPosition(player_position(playing ? start : seek));
                             if (playing)
                                 player.play();
                             else
                                 player.pause();
                             if (playing || !player.hasVideo() || expected < 0)
                                 announce();
                         } else if (status == QMediaPlayer::EndOfMedia && !ended) {
                             ended = true;
                             send({{"type", "ended"}});
                         }
                     });
    QObject::connect(&video, &QVideoSink::videoFrameChanged, &app, [&](const QVideoFrame &frame) {
        if (!frame.isValid() || !loaded || ended)
            return;
        const auto pts = frame.startTime() - shift, finish = frame.endTime() - shift;
        if (pts < -1500 || pts >= end * 1000)
            return;
        // The index chooses the held frame. Nominal endTime is not a VFR hold interval.
        if (!playing && (expected < 0 || std::abs(pts - expected) > 2))
            return;
        if (playing && finish > 0 && finish <= start * 1000)
            return;
        QJsonObject message{{"type", "video"}, {"pts_us", pts}, {"end_us", finish}};
        if (!image_clock.isValid() || image_clock.elapsed() >= 30 || !playing) {
            if (playing && socket.bytesToWrite() >= 2 * 1024 * 1024) {
                send(message);
                return;
            }
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
        if (!playing)
            announce();
    });
    QObject::connect(&buffers, &QAudioBufferOutput::audioBufferReceived, &app,
                     [&](const QAudioBuffer &buffer) {
                         if (loaded && !ended && buffer.isValid() && buffer.startTime() >= 0)
                             send({{"type", "audio"},
                                   {"pts_us", buffer.startTime() - shift},
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
                if (loaded && !playing)
                    player.setPosition(player_position(resume));
                playing = true;
                if (loaded) {
                    player.play();
                    announce();
                }
            } else if (command == "pause") {
                if (playing && loaded)
                    resume = std::max<qint64>(0, player.position() - shift / 1000);
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
        if (!loaded || ended || !ready)
            return;
        const auto position =
            playing ? std::max<qint64>(0, player.position() - shift / 1000) : resume;
        if (playing && position >= end) {
            player.pause();
            ended = true;
            send({{"type", "ended"}});
            return;
        }
        send({{"type", "position"}, {"ms", position}});
    });
    clock.start();
    socket.connectToServer(args[1]);
    QTimer::singleShot(static_cast<int>(load_timeout), &app, [&] {
        if (!ready) {
            fail("Cannot retrieve the indexed frame before the preview deadline.");
            app.exit(5);
        }
    });
    return app.exec();
}
