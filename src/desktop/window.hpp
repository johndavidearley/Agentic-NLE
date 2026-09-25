#pragma once
#include "commands/editor.hpp"
#include "desktop/timeline.hpp"
#include "desktop/transport.hpp"
#include "export/export.hpp"
#include "media/probe.hpp"
#include "project/document.hpp"
#include <QComboBox>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QProgressDialog>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <atomic>
namespace nle::desktop {
struct ImportResult {
    std::optional<media::ProbeResult> result;
    QString error;
};
enum class RecoveryChoice { Ask, Recover, Discard };
struct ExportOutcome {
    std::optional<exporting::Result> result;
    QString error;
};
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
    void sequenceSettings();
    void exportSequence();
    void refreshSourceSelection();
    TimeRange selectedSourceRange(const MediaAsset &asset) const;
    QByteArray mediaDragPayload();
    void editAtRevision(const Command &command, std::uint64_t revision, const QString &label);
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
    MediaCache *mediaCache_;
    MediaLibrary *library_;
    QLineEdit *sourceIn_, *sourceOut_;
    MediaId sourceSelected_{};
    QLabel *preview_, *clock_, *notice_, *sourceInfo_, *transportInfo_;
    QComboBox *sequences_;
    QSlider *scrub_;
    QLineEdit *positionEdit_, *inEdit_, *durationEdit_;
    QPushButton *play_, *undo_, *redo_, *cancelImport_;
    QImage image_;
    QFutureWatcher<ImportResult> watcher_;
    QFutureWatcher<ExportOutcome> exportWatcher_;
    QProgressDialog *exportDialog_ = nullptr;
    std::shared_ptr<std::atomic_bool> exportStop_;
    std::shared_ptr<std::atomic_bool> importStop_;
};
} // namespace nle::desktop
