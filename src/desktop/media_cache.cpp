#include "desktop/media_cache.hpp"
#include "media/probe.hpp"
#include "project/persistence.hpp"
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtConcurrentRun>
#include <algorithm>
#include <cmath>
namespace nle::desktop {
namespace {
QString signature(const MediaAsset &asset) {
    const auto i = std::find_if(asset.locations.begin(), asset.locations.end(),
                                [](const auto &v) { return v.role == LocationRole::Original; });
    if (i == asset.locations.end())
        throw DomainError("Source is offline");
    const auto path = media::utf8_path(i->uri);
    const auto size = std::filesystem::file_size(path);
    const auto modified = std::filesystem::last_write_time(path).time_since_epoch().count();
    return QString::fromStdString(i->uri) + ":" + QString::number(static_cast<qulonglong>(size)) +
           ":" + QString::number(static_cast<qlonglong>(modified));
}
CacheData generate(const QString &worker, const MediaAsset &asset, ProjectId project,
                   std::uint64_t nextId, std::uint32_t stream, bool wave, RationalTime time,
                   CacheData previous, const std::shared_ptr<std::atomic_bool> &stop) {
    CacheData result;
    try {
        const auto identity = signature(asset);
        if (identity == previous.signature && previous.error.isEmpty()) {
            previous.checked = QDateTime::currentMSecsSinceEpoch();
            return previous;
        }
        if (stop->load())
            throw DomainError("Cache cancelled");
        ProjectSnapshot manifest;
        manifest.id = project;
        manifest.next_id = nextId;
        manifest.name = "Derived preview";
        manifest.media.push_back(asset);
        QJsonObject request{{"project", QString::fromStdString(serialize(manifest))},
                            {"stream", static_cast<int>(stream)},
                            {"wave", wave},
                            {"value", QString::number(time.value())},
                            {"rate", QString::number(time.rate())}};
        const auto bytes = QJsonDocument(request).toJson(QJsonDocument::Compact);
        if (bytes.size() > 1024 * 1024)
            throw DomainError("Source metadata exceeds cache request limit");
        QTemporaryDir temporary;
        if (!temporary.isValid())
            throw DomainError("Cannot create derived-media workspace");
        const auto path = temporary.filePath("request.json");
        QFile input(path);
        if (!input.open(QIODevice::WriteOnly) || input.write(bytes) != bytes.size())
            throw DomainError("Cannot write cache request");
        input.close();
        const auto output =
            media::run_process(media::utf8_path(worker.toStdString()),
                               {media::path_utf8(media::utf8_path(path.toStdString()))},
                               {std::chrono::seconds(8), 512 * 1024, std::cref(*stop)});
        const auto document = QJsonDocument::fromJson(QByteArray::fromStdString(output));
        if (!document.isObject())
            throw DomainError("Invalid derived-media response");
        const auto response = document.object();
        if (!response["error"].toString().isEmpty())
            throw DomainError(response["error"].toString().toStdString());
        if (wave) {
            const auto array = response["peaks"].toArray();
            if (array.isEmpty() || array.size() > 1000)
                throw DomainError("Invalid waveform response");
            for (const auto value : array) {
                const auto peak = value.toDouble(-1);
                if (!std::isfinite(peak) || peak < 0 || peak > 1)
                    throw DomainError("Invalid waveform peak");
                result.peaks.push_back(static_cast<float>(peak));
            }
        } else {
            if (!result.image.loadFromData(
                    QByteArray::fromBase64(response["image"].toString().toLatin1()), "JPG") ||
                result.image.width() != 160 || result.image.height() != 90)
                throw DomainError("Invalid thumbnail response");
        }
        if (identity != signature(asset))
            throw DomainError("Source changed while generating its cache");
        result.signature = identity;
    } catch (const std::exception &e) {
        result = {};
        result.error = QString::fromUtf8(e.what()).left(1024);
    }
    result.checked = QDateTime::currentMSecsSinceEpoch();
    return result;
}
} // namespace
std::size_t CacheData::bytes() const {
    return static_cast<std::size_t>(image.sizeInBytes()) + peaks.size() * sizeof(float) +
           static_cast<std::size_t>((signature.size() + error.size()) * 2);
}
MediaCache::MediaCache(QString worker, QObject *parent)
    : QObject(parent), worker_(std::move(worker)) {
    for (auto &job : jobs_) {
        auto *slot = &job;
        connect(&job.watcher, &QFutureWatcher<Result>::finished, this, [this, slot] {
            auto result = slot->watcher.result();
            slot->active = false;
            if (result.generation == generation_) {
                pending_.erase(result.key);
                retain(std::move(result.key), std::move(result.data));
                emit ready();
            }
            start();
        });
    }
}
MediaCache::~MediaCache() {
    for (auto &job : jobs_)
        if (job.stop)
            job.stop->store(true);
}
void MediaCache::synchronize(const ProjectSnapshot &project) {
    next_id_ = project.next_id;
    if (project.id == project_ && project.media == assets_)
        return;
    ++generation_;
    for (auto &job : jobs_)
        if (job.stop)
            job.stop->store(true);
    project_ = project.id;
    assets_ = project.media;
    queue_.clear();
    pending_.clear();
    entries_.clear();
    bytes_ = 0;
}
const CacheData *MediaCache::request(MediaId asset, std::uint32_t stream, bool wave,
                                     RationalTime time) {
    const auto key = QString::number(asset.value) + ":" + QString::number(stream) + ":" +
                     QString::number(wave) + ":" + QString::number(time.value()) + "/" +
                     QString::number(time.rate());
    const auto found = entries_.find(key);
    if (found != entries_.end())
        found->second.used = ++used_;
    const bool expired = found == entries_.end() ||
                         QDateTime::currentMSecsSinceEpoch() - found->second.data.checked >
                             (found->second.data.error.isEmpty() ? 1000 : 5000);
    if (expired && !pending_.contains(key) && queue_.size() < queueLimit) {
        pending_.insert(key);
        queue_.push_back({key, asset, stream, wave, time});
        start();
    }
    return found == entries_.end() ? nullptr : &found->second.data;
}
void MediaCache::retain(QString key, CacheData value) {
    if (auto previous = entries_.find(key); previous != entries_.end()) {
        bytes_ -= previous->second.data.bytes();
        entries_.erase(previous);
    }
    const auto count = value.bytes();
    if (count > byteLimit)
        return;
    while (!entries_.empty() && (entries_.size() >= entryLimit || bytes_ + count > byteLimit)) {
        auto victim =
            std::min_element(entries_.begin(), entries_.end(), [](const auto &a, const auto &b) {
                return a.second.used < b.second.used;
            });
        bytes_ -= victim->second.data.bytes();
        entries_.erase(victim);
    }
    bytes_ += count;
    entries_.emplace(std::move(key), Entry{std::move(value), ++used_});
}
void MediaCache::start() {
    for (auto &job : jobs_) {
        if (job.active || queue_.empty())
            continue;
        const auto request = queue_.front();
        queue_.pop_front();
        const auto asset = std::find_if(assets_.begin(), assets_.end(),
                                        [&](const auto &a) { return a.id == request.asset; });
        if (asset == assets_.end()) {
            pending_.erase(request.key);
            continue;
        }
        CacheData previous;
        if (const auto old = entries_.find(request.key); old != entries_.end())
            previous = old->second.data;
        job.stop = std::make_shared<std::atomic_bool>(false);
        job.active = true;
        job.watcher.setFuture(QtConcurrent::run(
            [worker = worker_, asset = *asset, request, project = project_, nextId = next_id_,
             generation = generation_, previous = std::move(previous), stop = job.stop]() mutable {
                return Result{request.key, generation,
                              generate(worker, asset, project, nextId, request.stream, request.wave,
                                       request.time, std::move(previous), stop)};
            }));
    }
}
} // namespace nle::desktop
