#include "desktop/transport.hpp"
#include "media/probe.hpp"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QUuid>
#include <algorithm>
namespace nle::desktop {
Transport::Transport(QString worker, bool audible, QObject *parent)
    : QObject(parent), worker_(std::move(worker)), audible_(audible) {
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&server_, &QLocalServer::newConnection, this, [this] {
        if (socket_ || retiring_ || !process_) {
            auto *extra = server_.nextPendingConnection();
            if (extra)
                extra->deleteLater();
            return;
        }
        socket_ = server_.nextPendingConnection();
        if (!socket_)
            return;
        socket_->setReadBufferSize(4 * 1024 * 1024);
        server_.close();
        connect(socket_, &QLocalSocket::readyRead, this, &Transport::receive);
        connect(socket_, &QLocalSocket::disconnected, this, [this] {
            if (!retiring_ && process_)
                fail("Playback worker disconnected.");
        });
    });
    timer_.setInterval(10);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &Transport::tick);
    timer_.start();
}
Transport::~Transport() {
    if (socket_)
        socket_->disconnect(this);
    if (process_) {
        process_->disconnect(this);
        process_->kill();
        process_->waitForFinished(1000);
    }
}
void Transport::retire() {
    server_.close();
    input_.clear();
    segment_.reset();
    if (socket_) {
        socket_->disconnect(this);
        socket_->abort();
        socket_->deleteLater();
        socket_ = nullptr;
    }
    if (process_) {
        retiring_ = true;
        process_->kill();
    }
}
void Transport::cancel() {
    pending_ = false;
    playing_ = false;
    loading_ = false;
    gap_clock_.invalidate();
    retire();
    status_ = "Stopped";
    emit frameReady({}, -1, -1);
    emit changed();
}
void Transport::open(playback::Plan plan) {
    cancel();
    plan_ = std::move(plan);
    position_ = {};
    status_ = plan_.segments.empty() ? "Empty timeline" : "Ready";
    if (!plan_.segments.empty())
        seek({});
    else
        emit changed();
}
void Transport::seek(RationalTime position) {
    (void)plan_.sample(position);
    position_ = position;
    pending_ = true;
    loading_ = true;
    gap_clock_.invalidate();
    retire();
    emit frameReady({}, -1, -1);
    if (!process_)
        start();
    emit changed();
}
void Transport::play() {
    if (plan_.segments.empty())
        return;
    playing_ = true;
    if (position_ >= plan_.duration) {
        seek({});
        return;
    }
    if (socket_ && !retiring_) {
        send("play");
        status_ = loading_ ? "Loading preview" : "Playing";
        progress_clock_.restart();
    } else if (!process_ && !pending_) {
        pending_ = true;
        start();
    }
    emit changed();
}
void Transport::pause() {
    tick();
    playing_ = false;
    gap_clock_.invalidate();
    send("pause");
    status_ = loading_ ? "Loading preview" : "Paused";
    emit changed();
}
void Transport::send(const QString &command) {
    if (socket_ && !retiring_)
        socket_->write(
            QJsonDocument(QJsonObject{{"command", command}}).toJson(QJsonDocument::Compact) + '\n');
}
void Transport::fail(const QString &message) {
    cancel();
    status_ = message;
    emit changed();
}
void Transport::start() {
    if (!pending_ || process_)
        return;
    pending_ = false;
    retiring_ = false;
    const auto sample = plan_.sample(position_);
    if (!sample.segment) {
        loading_ = false;
        if (position_ >= plan_.duration) {
            playing_ = false;
            status_ = "End of sequence";
        } else {
            status_ = "Gap";
            gap_anchor_ = position_;
            gap_clock_.restart();
        }
        emit changed();
        return;
    }
    segment_ = sample.segment;
    const auto &segment = plan_.segments[*segment_];
    QString file;
    try {
        playback::validate_preview_source(segment.asset);
        const auto status = media::source_status(segment.asset);
        if (status != media::SourceStatus::Available)
            throw DomainError(std::string("Source is ") + media::status_name(status) +
                              "; relink it before preview.");
        for (const auto &location : segment.asset.locations)
            if (location.role == LocationRole::Original)
                file = QString::fromUtf8(location.uri);
    } catch (const std::exception &error) {
        fail(QString::fromUtf8(error.what()));
        return;
    }
    const auto name = "agentic-nle-" + QUuid::createUuid().toString(QUuid::Id128);
    if (!server_.listen(name)) {
        fail("Cannot create preview connection: " + server_.errorString());
        return;
    }
    process_ = new QProcess(this);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("QT_MEDIA_BACKEND", "ffmpeg");
    environment.insert("QT_FFMPEG_PROTOCOL_WHITELIST", "file");
    environment.insert("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", ",");
    environment.insert("QT_DISABLE_HW_TEXTURES_CONVERSION", "1");
    environment.insert("QT_QPA_PLATFORM", "offscreen");
    process_->setProcessEnvironment(environment);
    connect(process_, &QProcess::readyReadStandardError, this, [this] {
        if (process_)
            (void)process_->readAllStandardError();
    });
    connect(process_, &QProcess::readyReadStandardOutput, this, [this] {
        if (process_)
            (void)process_->readAllStandardOutput();
    });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process_->deleteLater();
            process_ = nullptr;
            fail("Cannot start playback worker. Check Qt runtime deployment.");
        }
    });
    connect(process_, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        auto *finished = process_.data();
        process_ = nullptr;
        if (finished)
            finished->deleteLater();
        if (!retiring_) {
            fail("Playback worker exited unexpectedly.");
            return;
        }
        retiring_ = false;
        if (pending_)
            start();
        emit changed();
    });
    loading_ = true;
    status_ = "Loading preview";
    deadline_.restart();
    progress_clock_.restart();
    process_->start(worker_, {name, QFileInfo(file).absoluteFilePath(),
                              QString::number(playback::milliseconds(sample.source)),
                              QString::number(playback::milliseconds(segment.source.end())),
                              playing_ ? "1" : "0", audible_ ? "1" : "0"});
}
void Transport::receive() {
    if (!socket_ || retiring_ || !segment_)
        return;
    input_ += socket_->readAll();
    if (input_.size() > 4 * 1024 * 1024) {
        fail("Preview transfer limit exceeded.");
        return;
    }
    for (qsizetype newline; (newline = input_.indexOf('\n')) >= 0;) {
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(input_.left(newline), &error);
        input_.remove(0, newline + 1);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            fail("Invalid preview worker message.");
            return;
        }
        const auto message = document.object();
        const auto type = message["type"].toString();
        emit observation(message);
        if (type == "error") {
            fail(message["message"].toString());
            return;
        }
        if (type == "ready") {
            loading_ = false;
            status_ = playing_ ? "Playing" : "Paused";
            progress_clock_.restart();
            send(playing_ ? "play" : "pause");
            emit changed();
        } else if (type == "video") {
            progress_clock_.restart();
            if (message.contains("image")) {
                QImage image;
                if (!image.loadFromData(
                        QByteArray::fromBase64(message["image"].toString().toLatin1()), "JPG")) {
                    fail("Invalid preview image.");
                    return;
                }
                emit frameReady(image, message["pts_us"].toInteger(),
                                message["end_us"].toInteger());
            }
        } else if (type == "position") {
            const auto source = RationalTime{std::max<qint64>(0, message["ms"].toInteger()), 1000};
            const auto &segment = plan_.segments[*segment_];
            const auto mapped = source < segment.source.start
                                    ? segment.position
                                    : segment.position + (source - segment.source.start);
            const auto next = std::min(mapped, segment.end());
            if (next != position_)
                progress_clock_.restart();
            position_ = next;
            emit changed();
        } else if (type == "ended") {
            const auto boundary = plan_.segments[*segment_].end();
            if (playing_)
                seek(boundary);
            else {
                position_ = boundary;
                pause();
            }
            return;
        }
        if (!socket_ || retiring_ || !segment_)
            return;
    }
}
void Transport::tick() {
    if (loading_ && process_ && !retiring_ && deadline_.isValid() &&
        deadline_.elapsed() > load_timeout_) {
        fail("Preview loading timed out; the worker was cancelled.");
        return;
    }
    if (playing_ && process_ && !loading_ && !retiring_ && progress_clock_.elapsed() > 5000) {
        fail("Preview stalled; the worker was cancelled.");
        return;
    }
    if (playing_ && gap_clock_.isValid() && !process_ && !pending_) {
        const auto boundary = plan_.sample(gap_anchor_).next_boundary;
        position_ = std::min(gap_anchor_ + RationalTime{gap_clock_.elapsed(), 1000}, boundary);
        if (position_ >= boundary)
            seek(boundary);
        else
            emit changed();
    }
}
} // namespace nle::desktop
