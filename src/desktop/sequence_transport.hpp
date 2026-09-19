#pragma once
#include "playback/sequence.hpp"
#include <QElapsedTimer>
#include <QImage>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QProcess>
#include <QTimer>
namespace nle::desktop {
class SequenceTransport : public QObject {
    Q_OBJECT
  public:
    SequenceTransport(QString worker, bool audible, QObject *parent = nullptr);
    ~SequenceTransport() override;
    void open(ProjectSnapshot project, SequenceId sequence);
    void seek(RationalTime position);
    void play();
    void pause();
    void cancel();
    void step(int direction);
    RationalTime position() const { return position_; }
    RationalTime duration() const { return plan_.duration; }
    bool playing() const { return playing_; }
    bool loading() const { return loading_; }
    bool idle() const { return process_ == nullptr; }
    QString status() const { return status_; }
    void setLoadTimeout(int ms) { timeout_ = ms; }
    void observePcm(bool value) { observe_ = value; }
  signals:
    void changed();
    void frameReady(const QImage &image, qint64 pts, qint64 end);
    void observation(const QJsonObject &message);

  private:
    void start();
    void retire();
    void receive();
    void fail(const QString &message);
    void send(QJsonObject object);
    QString worker_, status_ = "Ready";
    bool audible_, playing_ = false, loading_ = false, pending_ = false, retiring_ = false,
                   observe_ = false;
    int timeout_ = 20000;
    std::uint64_t generation_ = 0;
    ProjectSnapshot project_{};
    playback::SequencePlan plan_{};
    RationalTime position_;
    QLocalServer server_;
    QPointer<QLocalSocket> socket_;
    QPointer<QProcess> process_;
    QByteArray input_;
    QTimer timer_;
    QElapsedTimer deadline_, progress_;
};
} // namespace nle::desktop
