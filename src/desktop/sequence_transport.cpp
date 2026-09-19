#include "desktop/sequence_transport.hpp"
#include "project/persistence.hpp"
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QUuid>
#include <algorithm>
namespace nle::desktop {
SequenceTransport::SequenceTransport(QString worker, bool audible, QObject *parent)
    : QObject(parent), worker_(std::move(worker)), audible_(audible) {
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&server_, &QLocalServer::newConnection, this, [this] {
        auto *connection = server_.nextPendingConnection();
        if (!connection)
            return;
        if (socket_ || retiring_ || !process_) {
            connection->deleteLater();
            return;
        }
        socket_ = connection;
        server_.close();
        socket_->setReadBufferSize(4 * 1024 * 1024);
        connect(socket_, &QLocalSocket::readyRead, this, &SequenceTransport::receive);
        connect(socket_, &QLocalSocket::disconnected, this, [this] {
            if (!retiring_ && process_)
                fail("Playback worker disconnected");
        });
        send({{"command", "open"},
              {"project", QString::fromStdString(serialize(project_))},
              {"sequence", QString::number(plan_.id.value)},
              {"value", QString::number(position_.value())},
              {"rate", QString::number(position_.rate())},
              {"audible", audible_},
              {"observe_pcm", observe_}});
    });
    timer_.setInterval(20);
    connect(&timer_, &QTimer::timeout, this, [this] {
        if (process_ && !retiring_ && loading_ && deadline_.elapsed() > timeout_)
            fail("Preview loading timed out; worker cancelled");
        else if (process_ && !retiring_ && playing_ && !loading_ && progress_.elapsed() > 5000)
            fail("Preview stalled; worker cancelled");
    });
    timer_.start();
}
SequenceTransport::~SequenceTransport() {
    if (socket_)
        socket_->disconnect(this);
    if (process_) {
        process_->disconnect(this);
        process_->kill();
        process_->waitForFinished(1000);
    }
}
void SequenceTransport::send(QJsonObject object) {
    if (!socket_ || retiring_)
        return;
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    if (bytes.size() > 20 * 1024 * 1024) {
        fail("Preview project exceeds transfer limit");
        return;
    }
    socket_->write(bytes);
}
void SequenceTransport::retire() {
    ++generation_;
    server_.close();
    input_.clear();
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
void SequenceTransport::cancel() {
    pending_ = playing_ = loading_ = false;
    retire();
    status_ = "Stopped";
    emit frameReady({}, -1, -1);
    emit changed();
}
void SequenceTransport::open(ProjectSnapshot project, SequenceId id) {
    auto plan = playback::make_sequence_plan(project, id);
    if (plan.duration > RationalTime{86400} || plan.frame_duration < RationalTime{1, 60} ||
        plan.frame_duration > RationalTime{1} || plan.frame_duration.value() > 1000000000 ||
        plan.frame_duration.rate() > 1000000000)
        throw DomainError("Preview supports up to 24 hours and output rates from 1 to 60 fps");
    cancel();
    project_ = std::move(project);
    plan_ = std::move(plan);
    position_ = {};
    if (plan_.duration != RationalTime{})
        seek({});
    else {
        status_ = "Empty timeline";
        emit changed();
    }
}
void SequenceTransport::seek(RationalTime position) {
    (void)plan_.evaluate(position);
    position_ = position;
    pending_ = loading_ = true;
    retire();
    emit frameReady({}, -1, -1);
    if (!process_)
        start();
    emit changed();
}
void SequenceTransport::play() {
    if (plan_.duration == RationalTime{})
        return;
    playing_ = true;
    if (position_ >= plan_.duration) {
        seek({});
        return;
    }
    if (!process_ && !pending_)
        seek(position_);
    else if (socket_ && !loading_ && !retiring_)
        send({{"command", "play"}});
    status_ = loading_ ? "Loading preview" : "Playing";
    progress_.restart();
    emit changed();
}
void SequenceTransport::pause() {
    playing_ = false;
    if (plan_.id.value)
        seek(position_);
    else
        emit changed();
}
void SequenceTransport::step(int direction) {
    if (loading_ || !plan_.id.value)
        return;
    const auto f = plan_.frame_duration;
    const long double value = static_cast<long double>(position_.value()) * f.rate() /
                              (static_cast<long double>(position_.rate()) * f.value());
    auto index = static_cast<std::int64_t>(value);
    while (RationalTime{index * f.value(), f.rate()} > position_ && index > 0)
        --index;
    while (RationalTime{(index + 1) * f.value(), f.rate()} <= position_)
        ++index;
    const RationalTime target{index * f.value(), f.rate()};
    if (direction >= 0)
        ++index;
    else if (target >= position_ && index > 0)
        --index;
    playing_ = false;
    seek(std::min(RationalTime{index * f.value(), f.rate()}, plan_.duration));
}
void SequenceTransport::fail(const QString &message) {
    cancel();
    status_ = message;
    emit changed();
}
void SequenceTransport::start() {
    if (!pending_ || process_)
        return;
    pending_ = false;
    if (position_ >= plan_.duration) {
        loading_ = playing_ = false;
        status_ = "End of sequence";
        emit changed();
        return;
    }
    const auto name = "agentic-nle-sequence-" + QUuid::createUuid().toString(QUuid::Id128);
    if (!server_.listen(name)) {
        fail("Cannot create playback connection: " + server_.errorString());
        return;
    }
    auto *process = new QProcess(this);
    process_ = process;
    retiring_ = false;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("QT_QPA_PLATFORM", "offscreen");
    process->setProcessEnvironment(environment);
    connect(process, &QProcess::readyReadStandardError, this,
            [process] { (void)process->readAllStandardError(); });
    connect(process, &QProcess::readyReadStandardOutput, this,
            [process] { (void)process->readAllStandardOutput(); });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (process_ == process && error == QProcess::FailedToStart) {
            process_ = nullptr;
            process->deleteLater();
            fail("Cannot start sequence worker; check runtime deployment");
        }
    });
    connect(process, &QProcess::finished, this, [this, process](int, QProcess::ExitStatus) {
        if (process_ != process)
            return;
        process_ = nullptr;
        process->deleteLater();
        if (!retiring_) {
            fail("Playback worker exited unexpectedly");
            return;
        }
        retiring_ = false;
        if (pending_)
            start();
        emit changed();
    });
    loading_ = true;
    status_ = "Loading multi-track preview";
    deadline_.restart();
    progress_.restart();
    process->start(worker_, {name});
}
void SequenceTransport::receive() {
    if (!socket_ || retiring_)
        return;
    input_ += socket_->readAll();
    if (input_.size() > 4 * 1024 * 1024) {
        fail("Preview transfer limit exceeded");
        return;
    }
    for (qsizetype newline; (newline = input_.indexOf('\n')) >= 0;) {
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(input_.left(newline), &error);
        input_.remove(0, newline + 1);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            fail("Invalid worker message");
            return;
        }
        const auto msg = doc.object();
        const auto type = msg["type"].toString();
        const auto generation = generation_;
        emit observation(msg);
        if (generation != generation_)
            return;
        if (type == "error") {
            fail(msg["message"].toString());
            return;
        }
        if (type == "ready") {
            loading_ = false;
            status_ = playing_ ? "Playing" : "Paused";
            progress_.restart();
            if (playing_)
                send({{"command", "play"}});
            emit changed();
        } else if (type == "video") {
            QImage image;
            if (!image.loadFromData(QByteArray::fromBase64(msg["image"].toString().toLatin1()),
                                    "JPG") ||
                image.width() > 960 || image.height() > 540) {
                fail("Invalid preview image");
                return;
            }
            emit frameReady(image, msg["pts_us"].toInteger(), msg["end_us"].toInteger());
        } else if (type == "position" && playing_) {
            const auto sample = msg["sample"].toInteger(-1);
            if (sample < 0) {
                fail("Invalid playback clock");
                return;
            }
            position_ = std::min(RationalTime{sample, 48000}, plan_.duration);
            progress_.restart();
            emit changed();
        } else if (type == "ended") {
            position_ = plan_.duration;
            playing_ = loading_ = false;
            status_ = "End of sequence";
            retire();
            emit frameReady({}, -1, -1);
            emit changed();
            return;
        }
        if (!socket_ || retiring_)
            return;
    }
}
} // namespace nle::desktop
