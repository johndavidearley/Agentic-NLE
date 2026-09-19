#pragma once
#include "commands/editor.hpp"
#include "desktop/transport.hpp"
#include "media/probe.hpp"
#include "project/document.hpp"
#include <QComboBox>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <atomic>
namespace nle::desktop {
class Timeline : public QWidget {
    Q_OBJECT
  public:
    explicit Timeline(QWidget *parent = nullptr) : QWidget(parent) { setMinimumHeight(200); }
    void display(ProjectSnapshot project, SequenceId sequence, ClipId selected);
    void setPosition(RationalTime position) {
        position_ = position;
        update();
    }
  signals:
    void selected(ClipId clip);
    void sought(RationalTime position);

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;

  private:
    ProjectSnapshot project_{};
    SequenceId sequence_{};
    ClipId selected_{};
    RationalTime position_{};
    double scale() const;
};
struct ImportResult {
    std::optional<media::ProbeResult> result;
    QString error;
};
enum class RecoveryChoice { Ask, Recover, Discard };
class Window : public QMainWindow {
    Q_OBJECT
  public:
    Window(QString worker, QString ffprobe, bool audible = true, QString recoveryDirectory = {});
    ~Window() override;
    ProjectSnapshot snapshot() const { return editor_->snapshot(); }
    void importMedia(const QString &path);
    bool importing() const { return watcher_.isRunning(); }
    void appendSelected();
    void openProject(const QString &path, RecoveryChoice choice = RecoveryChoice::Ask);
    bool checkpointRecovery();
    bool restoreDrafts(RecoveryChoice choice = RecoveryChoice::Ask);
    void saveProject(const QString &path);
    void setPlayhead(RationalTime position) { transport_.seek(position); }
    Transport &transport() { return transport_; }

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    void placeSelected();
    void playbackSettings();
    void refresh();
    void refreshInspector();
    void selectClip(ClipId id);
    void edit(const Command &command, const std::string &label);
    void report(const QString &message);
    bool mayDiscard();
    void startDraft();
    void openDocument(const QString &path, RecoveryChoice choice, bool draft);
    const Clip *selectedClip(const ProjectSnapshot &project) const;
    std::unique_ptr<Editor> editor_;
    ProjectSnapshot saved_;
    std::unique_ptr<DocumentFile> document_;
    QString recoveryDirectory_;
    QTimer recoveryTimer_;
    SequenceId sequence_{};
    ClipId selected_{};
    QString path_, ffprobe_;
    Transport transport_;
    Timeline *timeline_;
    QListWidget *library_;
    QLabel *preview_, *clock_, *notice_, *sourceInfo_, *transportInfo_;
    QComboBox *sequences_;
    QSlider *scrub_;
    QLineEdit *positionEdit_, *inEdit_, *durationEdit_;
    QPushButton *play_, *undo_, *redo_, *cancelImport_;
    QImage image_;
    QFutureWatcher<ImportResult> watcher_;
    std::shared_ptr<std::atomic_bool> importStop_;
};
} // namespace nle::desktop
