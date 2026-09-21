#pragma once
#include "commands/timeline.hpp"
#include <QAbstractScrollArea>
#include <QListWidget>
#include <QPointF>
#include <functional>
#include <map>
namespace nle::desktop {
class MediaCache;
class MediaLibrary : public QListWidget {
    Q_OBJECT
  public:
    using QListWidget::QListWidget;
    std::function<QByteArray()> payload;

  protected:
    void startDrag(Qt::DropActions) override;
};
class Timeline : public QAbstractScrollArea {
    Q_OBJECT
  public:
    explicit Timeline(QWidget *parent = nullptr);
    void display(ProjectSnapshot project, SequenceId sequence, ClipId selected);
    void setPosition(RationalTime position);
    void setZoom(double pixelsPerSecond);
    double zoom() const { return pixels_; }
    void setSnapping(bool enabled) {
        snapping_ = enabled;
        viewport()->update();
    }
    bool snapping() const { return snapping_; }
    bool gesturing() const { return preview_ != nullptr; }
    void cancelGesture();
    void setCache(MediaCache *cache) { cache_ = cache; }
    QRectF clipRect(ClipId clip) const;
    RationalTime timeAt(double x) const;
    static constexpr int headerWidth = 238, rulerHeight = 30, rowHeight = 86;
  signals:
    void selected(ClipId clip);
    void sought(RationalTime position);
    void editRequested(const Command &command, std::uint64_t revision, const QString &label);
    void rejected(const QString &reason);
    void gestureStarted();
    void playRequested();
    void stepRequested(int direction);
    void zoomChanged(double pixelsPerSecond);

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void scrollContentsBy(int, int) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dragLeaveEvent(QDragLeaveEvent *) override;
    void dropEvent(QDropEvent *) override;

  private:
    const Sequence *sequence() const;
    const Track *trackAt(double y) const;
    const Clip *findClip(ClipId id, TrackId *track = nullptr) const;
    double xAt(RationalTime time) const;
    int yAt(std::size_t row) const;
    void ranges();
    void headers();
    void previewCommand(const Command &command, TrackId track);
    void updateDrag(QPointF point);
    void finishGesture();
    void zoomAt(double pixels, double anchor);
    void updateDrop(QPointF point);
    void requestTrack(TrackId id, const std::function<void(TrackPlayback &)> &change);
    ProjectSnapshot project_{};
    SequenceId sequence_{};
    ClipId selected_{};
    RationalTime position_{}, grab_{};
    double pixels_ = 80;
    bool snapping_ = true, moved_ = false, inserting_ = false;
    QPointF press_;
    Clip initial_{};
    TrackId initialTrack_{}, ghostTrack_{};
    ClipGesture gesture_ = ClipGesture::Move;
    std::unique_ptr<Editor> preview_;
    std::optional<Command> command_;
    std::optional<Clip> ghost_;
    std::optional<InsertClip> insertion_;
    QString error_;
    std::map<TrackId, QWidget *> headers_;
    MediaCache *cache_ = nullptr;
};
} // namespace nle::desktop
