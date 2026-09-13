#pragma once
#include "playback/plan.hpp"
#include <QElapsedTimer>
#include <QImage>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QProcess>
#include <QTimer>
namespace nle::desktop {
class Transport : public QObject {
    Q_OBJECT
  public:
    explicit Transport(QString worker, bool audible = true, QObject *parent = nullptr);
    ~Transport() override;
    void open(playback::Plan plan);
    void seek(RationalTime position);
    void play();
    void pause();
    void cancel();
    RationalTime position() const { return position_; }
    RationalTime duration() const { return plan_.duration; }
    bool playing() const { return playing_; }
    bool loading() const { return loading_; }
    QString status() const { return status_; }
    bool idle() const { return process_ == nullptr; }
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
    QString worker_, status_ = "Open a project or import media";
    bool audible_, playing_ = false, loading_ = false, pending_ = false, retiring_ = false;
    int load_timeout_ = 10000;
    std::optional<std::size_t> segment_;
    QLocalServer server_;
    QPointer<QLocalSocket> socket_;
    QPointer<QProcess> process_;
    QByteArray input_;
    QElapsedTimer deadline_, gap_clock_, progress_clock_;
    QTimer timer_;
};
} // namespace nle::desktop
