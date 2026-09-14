#pragma once
#include "media/frame_index.hpp"
#include "playback/plan.hpp"
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QImage>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
namespace nle::desktop {
struct IndexResult {
    std::optional<media::FrameIndex> index;
    QString error;
    QString preview_file;
    SourceMetadata preview_source;
    std::shared_ptr<QTemporaryDir> temporary;
};
class Transport : public QObject {
    Q_OBJECT
  public:
    explicit Transport(QString worker, bool audible = true, QObject *parent = nullptr,
                       QString ffprobe = "ffprobe");
    ~Transport() override;
    void open(playback::Plan plan);
    void seek(RationalTime position);
    void play();
    void pause();
    void step(int direction);
    void setProbeTool(QString path);
    void cancel();
    RationalTime position() const { return position_; }
    RationalTime duration() const { return plan_.duration; }
    bool playing() const { return playing_; }
    bool loading() const { return loading_; }
    QString status() const { return status_; }
    bool idle() const { return process_ == nullptr && !indexing_; }
    void setLoadTimeout(int milliseconds) { load_timeout_ = milliseconds; }
  signals:
    void changed();
    void frameReady(const QImage &image, qint64 sourcePts, qint64 sourceEnd);
    void observation(const QJsonObject &message);

  private:
    void start();
    void retire();
    void send(const QString &command);
    void receive();
    void fail(const QString &message);
    void tick();
    playback::Plan plan_;
    RationalTime position_, gap_anchor_;
    QString worker_, ffprobe_, status_ = "Open a project or import media";
    bool audible_, playing_ = false, loading_ = false, pending_ = false, retiring_ = false;
    int load_timeout_ = 10000;
    std::optional<std::size_t> segment_;
    QLocalServer server_;
    QPointer<QLocalSocket> socket_;
    QPointer<QProcess> process_;
    QByteArray input_;
    QElapsedTimer deadline_, gap_clock_, progress_clock_;
    QTimer timer_;
    struct CachedIndex {
        QString file;
        SourceMetadata source;
        std::filesystem::file_time_type modified;
        media::FrameIndex index;
        QString preview_file;
        SourceMetadata preview_source;
        std::shared_ptr<QTemporaryDir> temporary;
    };
    std::optional<CachedIndex> cache_;
    QFutureWatcher<IndexResult> index_watcher_;
    std::shared_ptr<std::atomic_bool> index_stop_;
    bool indexing_ = false;
    bool prepareIndex(const QString &file, const SourceMetadata &source);
};
} // namespace nle::desktop
