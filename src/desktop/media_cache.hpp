#pragma once
#include "project/model.hpp"
#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QString>
#include <array>
#include <atomic>
#include <deque>
#include <map>
#include <set>
namespace nle::desktop {
struct CacheData {
    QImage image;
    std::vector<float> peaks; // 100 peak bins/second, stereo maximum, at most ten seconds.
    QString signature, error;
    qint64 checked = 0;
    std::size_t bytes() const;
};
class MediaCache : public QObject {
    Q_OBJECT
  public:
    explicit MediaCache(QString worker, QObject *parent = nullptr);
    ~MediaCache() override;
    void synchronize(const ProjectSnapshot &project);
    const CacheData *request(MediaId asset, std::uint32_t stream, bool waveform, RationalTime time);
    std::size_t residentBytes() const { return bytes_; }
    std::size_t entries() const { return entries_.size(); }
    std::size_t pending() const { return pending_.size(); }
    static constexpr std::size_t byteLimit = 16 * 1024 * 1024, entryLimit = 256, queueLimit = 32;
  signals:
    void ready();

  private:
    struct Request {
        QString key;
        MediaId asset;
        std::uint32_t stream;
        bool wave;
        RationalTime time;
    };
    struct Entry {
        CacheData data;
        std::uint64_t used;
    };
    struct Result {
        QString key;
        std::uint64_t generation;
        CacheData data;
    };
    struct Job {
        QFutureWatcher<Result> watcher;
        std::shared_ptr<std::atomic_bool> stop;
        bool active = false;
    };
    void start();
    void retain(QString key, CacheData value);
    QString worker_;
    ProjectId project_{};
    std::uint64_t next_id_ = 1, generation_ = 0, used_ = 0;
    std::vector<MediaAsset> assets_;
    std::map<QString, Entry> entries_;
    std::deque<Request> queue_;
    std::set<QString> pending_;
    std::array<Job, 2> jobs_;
    std::size_t bytes_ = 0;
};
} // namespace nle::desktop
