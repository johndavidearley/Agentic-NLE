#include "desktop/transport.hpp"
#include "media/probe.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QUuid>
#include <QtConcurrentRun>
#include <algorithm>
#include <limits>
namespace nle::desktop {
namespace {
qint64 microseconds(RationalTime time) {
    if (time.value() > std::numeric_limits<std::int64_t>::max() / 1000000)
        throw DomainError("preview timestamp conversion overflow");
    return time.value() * 1000000 / time.rate();
}
qint64 playerOffset(const SourceMetadata &source) {
    const auto origin = source_origin(source);
    const auto container = source.container_start.value_or(origin);
    return origin >= container ? microseconds(origin.since(container))
                               : -microseconds(container.since(origin));
}
} // namespace
Transport::Transport(QString worker, bool audible, QObject *parent, QString ffprobe)
    : QObject(parent), worker_(std::move(worker)), ffprobe_(std::move(ffprobe)), audible_(audible) {
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
    if (index_stop_)
        index_stop_->store(true);
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
    if (sequence_transport_)
        sequence_transport_->cancel();
    if (index_stop_)
        index_stop_->store(true);
    pending_ = false;
    playing_ = false;
    loading_ = false;
    gap_clock_.invalidate();
    retire();
    status_ = "Stopped";
    emit frameReady({}, -1, -1);
    emit changed();
}
void Transport::open(ProjectSnapshot project, SequenceId sequence) {
    cancel();
    if (!sequence_transport_) {
        auto worker = QFileInfo(worker_).dir().filePath("nle-sequence-worker");
#ifdef _WIN32
        worker += ".exe";
#endif
        sequence_transport_ = std::make_unique<SequenceTransport>(worker, audible_, this);
        sequence_transport_->setLoadTimeout(load_timeout_);
        connect(sequence_transport_.get(), &SequenceTransport::changed, this, &Transport::changed);
        connect(sequence_transport_.get(), &SequenceTransport::frameReady, this,
                &Transport::frameReady);
        connect(sequence_transport_.get(), &SequenceTransport::observation, this,
                &Transport::observation);
    }
    sequence_mode_ = true;
    sequence_transport_->open(std::move(project), sequence);
}
void Transport::open(playback::Plan plan) {
    cancel();
    sequence_mode_ = false;
    plan_ = std::move(plan);
    position_ = {};
    status_ = plan_.segments.empty() ? "Empty timeline" : "Ready";
    if (!plan_.segments.empty())
        seek({});
    else
        emit changed();
}
void Transport::seek(RationalTime position) {
    if (sequence_mode_) {
        sequence_transport_->seek(position);
        return;
    }
    if (index_stop_ && indexing_)
        index_stop_->store(true);
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
    if (sequence_mode_) {
        sequence_transport_->play();
        return;
    }
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
    if (sequence_mode_) {
        sequence_transport_->pause();
        return;
    }
    tick();
    playing_ = false;
    gap_clock_.invalidate();
    if (!plan_.segments.empty())
        seek(position_);
    else {
        status_ = "Paused";
        emit changed();
    }
}
void Transport::setProbeTool(QString path) {
    cancel();
    ffprobe_ = std::move(path);
    cache_.reset();
}
void Transport::step(int direction) {
    if (sequence_mode_) {
        sequence_transport_->step(direction);
        return;
    }
    if (loading_ || !cache_)
        return;
    auto sample = plan_.sample(position_);
    if (!sample.segment) {
        if (direction >= 0 || position_ != plan_.duration || plan_.segments.empty())
            return;
        sample.segment = plan_.segments.size() - 1;
        sample.source = plan_.segments.back().source.end();
    }
    const auto &segment = plan_.segments[*sample.segment];
    if (segment.asset.kind == MediaKind::Audio)
        return;
    const auto original =
        std::find_if(segment.asset.locations.begin(), segment.asset.locations.end(),
                     [](const auto &location) { return location.role == LocationRole::Original; });
    if (!segment.asset.source || cache_->source != *segment.asset.source ||
        original == segment.asset.locations.end() ||
        cache_->file != QString::fromUtf8(original->uri)) {
        status_ = "Seek into this clip before stepping its frames.";
        emit changed();
        return;
    }
    const auto target = cache_->index.step(sample.source, direction, segment.source);
    if (target) {
        playing_ = false;
        seek(segment.position + (*target - segment.source.start));
    }
}
bool Transport::prepareIndex(const QString &file, const SourceMetadata &source) {
    const auto path = media::utf8_path(file.toStdString());
    const auto modified = std::filesystem::last_write_time(path);
    if (cache_ && cache_->file == file && cache_->source == source && cache_->modified == modified)
        return true;
    cache_.reset();
    pending_ = true;
    loading_ = true;
    status_ = "Preparing source timing";
    index_stop_ = std::make_shared<std::atomic_bool>(false);
    const auto stop = index_stop_;
    const auto tool = media::utf8_path(ffprobe_.toStdString());
    indexing_ = true;
    index_watcher_.disconnect(this);
    connect(&index_watcher_, &QFutureWatcher<IndexResult>::finished, this,
            [this, file, source, modified, stop] {
                indexing_ = false;
                const auto result = index_watcher_.result();
                if (!stop->load()) {
                    if (!result.index) {
                        fail(result.error);
                        return;
                    }
                    cache_ = CachedIndex{file,
                                         source,
                                         modified,
                                         *result.index,
                                         result.preview_file,
                                         result.preview_source,
                                         result.temporary};
                }
                if (pending_)
                    start();
                emit changed();
            });
    index_watcher_.setFuture(QtConcurrent::run([path, source, tool, stop] {
        try {
            const auto began = std::chrono::steady_clock::now();
            const auto budget = [&] {
                const auto remaining = std::chrono::seconds(30) -
                                       std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now() - began);
                if (remaining <= std::chrono::milliseconds::zero())
                    throw DomainError("Frame preparation timed out");
                return media::ProcessOptions{remaining, media::max_index_bytes, std::cref(*stop)};
            };
            auto index = media::index_frames(path, source, tool, budget());
            IndexResult result{
                std::move(index), {}, QString::fromStdString(media::path_utf8(path)), source, {}};
            // Qt's demuxer cannot seek into negative packet timestamps. Rebase a temporary
            // packet-copy source; keep the original asset and exact source index authoritative.
            if (source_origin(source).negative()) {
                result.temporary = std::make_shared<QTemporaryDir>();
                if (!result.temporary->isValid())
                    throw DomainError("Cannot create temporary playback directory");
                result.preview_file = result.temporary->filePath("preview.mkv");
                const auto prepared = media::utf8_path(result.preview_file.toStdString());
                const auto ffmpeg =
                    tool.parent_path() / (tool.extension() == ".exe" ? "ffmpeg.exe" : "ffmpeg");
                std::vector<std::string> args{
                    "-v",   "error",   "-nostdin", "-protocol_whitelist",
                    "file", "-copyts", "-i",       media::path_utf8(path)};
                for (const auto &stream : source.streams) {
                    args.push_back("-map");
                    args.push_back("0:" + std::to_string(stream.index));
                }
                const std::vector<std::string> output{
                    "-c",        "copy", "-avoid_negative_ts",      "make_non_negative", "-fs",
                    "536870912", "-y",   media::path_utf8(prepared)};
                args.insert(args.end(), output.begin(), output.end());
                (void)media::run_process(ffmpeg, args, budget());
                if (std::filesystem::file_size(prepared) > 537919488)
                    throw DomainError("Temporary preview exceeds the 512 MiB packet-copy limit");
                result.preview_source = media::probe(prepared, tool, budget()).source;
                if (source_origin(result.preview_source).negative() ||
                    result.preview_source.streams.size() != source.streams.size())
                    throw DomainError("Cannot normalize this source's timestamps for preview");
                for (std::size_t i = 0; i < source.streams.size(); ++i)
                    if (source.streams[i].kind != result.preview_source.streams[i].kind ||
                        stream_offset(source, source.streams[i]) !=
                            stream_offset(result.preview_source, result.preview_source.streams[i]))
                        throw DomainError("Temporary preview changed stream alignment");
                for (std::size_t i = 0; i < source.streams.size(); ++i) {
                    const auto before =
                        stream_duration(source.streams[i], source.container_duration);
                    const auto after = stream_duration(result.preview_source.streams[i],
                                                       result.preview_source.container_duration);
                    if ((before > after ? before - after : after - before) > RationalTime{1, 1000})
                        throw DomainError(
                            "Temporary preview changed stream duration or was truncated");
                }
                const auto verification =
                    media::index_frames(prepared, result.preview_source, tool, budget());
                if (verification.frames != result.index->frames)
                    throw DomainError(
                        "Temporary preview changed decoded frame timestamps or was truncated");
            }
            return result;
        } catch (const std::exception &error) {
            return IndexResult{{}, QString::fromUtf8(error.what()), {}, {}, {}};
        }
    }));
    emit changed();
    return false;
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
    if (!pending_ || process_ || indexing_)
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
    qint64 poster = -1, seek_ms = playback::milliseconds(sample.source), shift = 0;
    try {
        playback::validate_preview_source(segment.asset);
        const auto status = media::source_status(segment.asset);
        if (status != media::SourceStatus::Available)
            throw DomainError(std::string("Source is ") + media::status_name(status) +
                              "; relink it before preview.");
        for (const auto &location : segment.asset.locations)
            if (location.role == LocationRole::Original)
                file = QString::fromUtf8(location.uri);
        shift = playerOffset(*segment.asset.source);
        if (segment.asset.kind != MediaKind::Audio ||
            source_origin(*segment.asset.source).negative()) {
            if (!prepareIndex(file, *segment.asset.source))
                return;
            file = cache_->preview_file;
            shift = playerOffset(cache_->preview_source);
            if (const auto selected = cache_->index.at(sample.source)) {
                const auto &frame = cache_->index.frames[*selected];
                poster = microseconds(frame.position);
                // Seek inside the selected frame's nominal interval, avoiding an exact
                // boundary where Qt can return the preceding frame. Verify decoded PTS.
                seek_ms = playback::milliseconds(frame.position) + 1;
            }
        }
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
                              playing_ ? "1" : "0", audible_ ? "1" : "0", QString::number(seek_ms),
                              QString::number(poster), QString::number(shift),
                              QString::number(load_timeout_)});
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
        } else if (type == "position" && playing_) {
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
    if (sequence_mode_)
        return;
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
