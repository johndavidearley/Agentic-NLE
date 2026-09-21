#include "desktop/timeline.hpp"
#include "desktop/media_cache.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegion>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
namespace nle::desktop {
namespace {
constexpr auto mime = "application/x-agentic-nle-media";
double seconds(RationalTime t) {
    return static_cast<double>(t.value()) / static_cast<double>(t.rate());
}
RationalTime fromSeconds(double value) {
    return {static_cast<std::int64_t>(std::llround(std::clamp(value, 0.0, 86400.0) * 1000000)),
            1000000};
}
} // namespace
void MediaLibrary::startDrag(Qt::DropActions) {
    if (!payload)
        return;
    const auto bytes = payload();
    if (bytes.isEmpty())
        return;
    QDrag drag(this);
    auto *mimeData = new QMimeData;
    mimeData->setData(mime, bytes);
    drag.setMimeData(mimeData);
    drag.exec(Qt::CopyAction);
}
Timeline::Timeline(QWidget *parent) : QAbstractScrollArea(parent) {
    setObjectName("timeline");
    setMinimumHeight(240);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip("Drag a clip or either edge. [ / ] select clips; S splits; Delete removes; Escape "
               "cancels. Ctrl+wheel zooms; Shift+wheel scrolls.");
    viewport()->setMouseTracking(true);
    viewport()->setAcceptDrops(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setAccessibleName("Timeline: brackets select clips; drag clips or edges; S split; Delete "
                      "remove; Escape cancel");
}
const Sequence *Timeline::sequence() const {
    for (const auto &s : project_.sequences)
        if (s.id == sequence_)
            return &s;
    return nullptr;
}
const Clip *Timeline::findClip(ClipId id, TrackId *out) const {
    if (const auto *s = sequence())
        for (const auto &track : s->tracks)
            for (const auto &clip : track.clips)
                if (clip.id == id) {
                    if (out)
                        *out = track.id;
                    return &clip;
                }
    return nullptr;
}
int Timeline::yAt(std::size_t row) const {
    return rulerHeight + static_cast<int>(row) * rowHeight - verticalScrollBar()->value();
}
double Timeline::xAt(RationalTime t) const {
    return headerWidth + seconds(t) * pixels_ - horizontalScrollBar()->value();
}
RationalTime Timeline::timeAt(double x) const {
    return fromSeconds((x - headerWidth + horizontalScrollBar()->value()) / pixels_);
}
const Track *Timeline::trackAt(double y) const {
    if (y < rulerHeight || y >= viewport()->height())
        return nullptr;
    const auto row =
        static_cast<std::size_t>((y - rulerHeight + verticalScrollBar()->value()) / rowHeight);
    const auto *s = sequence();
    return s && row < s->tracks.size() ? &s->tracks[row] : nullptr;
}
QRectF Timeline::clipRect(ClipId id) const {
    if (const auto *s = sequence())
        for (std::size_t row = 0; row < s->tracks.size(); ++row)
            for (const auto &clip : s->tracks[row].clips)
                if (clip.id == id)
                    return {xAt(clip.position), static_cast<double>(yAt(row) + 6),
                            std::max(2.0, seconds(clip.source.duration) * pixels_),
                            rowHeight - 12.0};
    return {};
}
void Timeline::display(ProjectSnapshot project, SequenceId id, ClipId selected) {
    if (preview_ &&
        (project.id != project_.id || project.revision != project_.revision || id != sequence_)) {
        cancelGesture();
        emit rejected("Project changed; the unfinished gesture was cancelled.");
    }
    const bool changed =
        project.id != project_.id || project.revision != project_.revision || id != sequence_;
    project_ = std::move(project);
    sequence_ = id;
    selected_ = selected;
    if (changed) {
        for (auto &[key, widget] : headers_) {
            (void)key;
            widget->hide();
            widget->deleteLater();
        }
        headers_.clear();
    }
    ranges();
    headers();
    viewport()->update();
}
void Timeline::setPosition(RationalTime value) {
    position_ = value;
    viewport()->update();
}
void Timeline::ranges() {
    double length = 120;
    int tracks = 0;
    if (const auto *s = sequence()) {
        tracks = static_cast<int>(s->tracks.size());
        for (const auto &track : s->tracks)
            if (!track.clips.empty())
                length = std::max(
                    length,
                    seconds(track.clips.back().position + track.clips.back().source.duration) + 30);
    }
    const int page = std::max(1, viewport()->width() - headerWidth);
    horizontalScrollBar()->setPageStep(page);
    horizontalScrollBar()->setRange(
        0, std::max(0, static_cast<int>(std::min(length, 86400.0) * pixels_) - page));
    verticalScrollBar()->setPageStep(std::max(1, viewport()->height() - rulerHeight));
    verticalScrollBar()->setRange(
        0, std::max(0, tracks * rowHeight - (viewport()->height() - rulerHeight)));
}
void Timeline::zoomAt(double value, double anchor) {
    const double time = (anchor - headerWidth + horizontalScrollBar()->value()) / pixels_;
    pixels_ = std::clamp(value, 2.0, 1200.0);
    ranges();
    horizontalScrollBar()->setValue(
        static_cast<int>(std::clamp(time * pixels_ - anchor + headerWidth, 0.0, 103680000.0)));
    emit zoomChanged(pixels_);
    viewport()->update();
}
void Timeline::setZoom(double value) { zoomAt(value, (viewport()->width() + headerWidth) / 2.0); }
void Timeline::resizeEvent(QResizeEvent *event) {
    QAbstractScrollArea::resizeEvent(event);
    ranges();
    headers();
}
void Timeline::scrollContentsBy(int, int) {
    headers();
    viewport()->update();
}
void Timeline::requestTrack(TrackId id, const std::function<void(TrackPlayback &)> &change) {
    if (const auto *s = sequence())
        for (const auto &track : s->tracks)
            if (track.id == id) {
                auto playback = track.playback;
                change(playback);
                emit editRequested(SetTrackPlayback{id, playback}, project_.revision,
                                   "Track playback");
                return;
            }
}
void Timeline::headers() {
    const auto *s = sequence();
    if (!s)
        return;
    std::vector<TrackId> visible;
    for (std::size_t row = 0; row < s->tracks.size(); ++row) {
        const int y = yAt(row);
        if (y + rowHeight <= rulerHeight || y >= viewport()->height())
            continue;
        const auto &track = s->tracks[row];
        visible.push_back(track.id);
        if (!headers_.contains(track.id)) {
            auto *box = new QWidget(viewport());
            box->setObjectName("trackHeader_" + QString::number(track.id.value));
            box->setAutoFillBackground(true);
            auto *name = new QLabel(QString::fromStdString(track.name), box);
            name->setGeometry(8, 2, 155, 23);
            name->setToolTip(QString::fromStdString(track.name));
            const auto reorder = [&](const QString &text, int x, int direction) {
                auto *b = new QPushButton(text, box);
                b->setGeometry(x, 1, 29, 24);
                b->setStyleSheet("padding:0px;font-size:16px;");
                b->setAccessibleName(direction < 0 ? "Move track up" : "Move track down");
                b->setObjectName(QString(direction < 0 ? "trackUp_" : "trackDown_") +
                                 QString::number(track.id.value));
                b->setEnabled(direction < 0 ? row > 0 : row + 1 < s->tracks.size());
                connect(b, &QPushButton::clicked, this, [this, id = track.id, direction] {
                    if (const auto *seq = sequence())
                        for (std::size_t i = 0; i < seq->tracks.size(); ++i)
                            if (seq->tracks[i].id == id) {
                                const auto at = static_cast<std::int64_t>(i) + direction;
                                if (at >= 0 && at < static_cast<std::int64_t>(seq->tracks.size()))
                                    emit editRequested(
                                        ReorderTrack{sequence_, id, static_cast<std::size_t>(at)},
                                        project_.revision, "Reorder track");
                                return;
                            }
                });
            };
            reorder(QString(QChar(0x2191)), 169, -1);
            reorder(QString(QChar(0x2193)), 200, 1);
            auto *enabled = new QCheckBox("On", box), *muted = new QCheckBox("Mute", box);
            enabled->setGeometry(8, 30, 56, 26);
            muted->setGeometry(65, 30, 68, 26);
            enabled->setObjectName("trackEnabled_" + QString::number(track.id.value));
            muted->setObjectName("trackMuted_" + QString::number(track.id.value));
            enabled->setAccessibleName("Enable track " + QString::fromStdString(track.name));
            muted->setAccessibleName("Mute track " + QString::fromStdString(track.name));
            enabled->setChecked(track.playback.enabled);
            muted->setChecked(track.playback.muted);
            connect(enabled, &QCheckBox::clicked, this, [this, id = track.id](bool v) {
                requestTrack(id, [v](auto &p) { p.enabled = v; });
            });
            connect(muted, &QCheckBox::clicked, this, [this, id = track.id](bool v) {
                requestTrack(id, [v](auto &p) { p.muted = v; });
            });
            auto *gain = new QSpinBox(box);
            gain->setGeometry(136, 29, 93, 29);
            gain->setRange(0, 4000);
            gain->setSingleStep(100);
            gain->setKeyboardTracking(false);
            gain->setValue(static_cast<int>(track.playback.gain_milli));
            gain->setObjectName("trackGain_" + QString::number(track.id.value));
            gain->setAccessibleName(QString::fromStdString(track.name) + " gain, 1000 is unity");
            gain->setToolTip("Linear audio gain: 1000 is unity; 0-4000");
            connect(gain, &QSpinBox::valueChanged, this, [this, id = track.id](int v) {
                requestTrack(id, [v](auto &p) { p.gain_milli = static_cast<std::uint32_t>(v); });
            });
            headers_[track.id] = box;
        }
        headers_.at(track.id)->setGeometry(0, y, headerWidth - 2, rowHeight - 2);
        headers_.at(track.id)->setMask(QRegion(QRect(
            0, std::max(0, rulerHeight - y), headerWidth - 2,
            std::min(rowHeight - 2, viewport()->height() - y) - std::max(0, rulerHeight - y))));
        headers_.at(track.id)->show();
    }
    for (auto i = headers_.begin(); i != headers_.end();) {
        if (std::find(visible.begin(), visible.end(), i->first) == visible.end()) {
            i->second->hide();
            i->second->deleteLater();
            i = headers_.erase(i);
        } else
            ++i;
    }
}
void Timeline::paintEvent(QPaintEvent *) {
    QPainter p(viewport());
    p.fillRect(viewport()->rect(), QColor("#141b25"));
    const auto *s = sequence();
    p.save();
    p.setClipRect(QRect(headerWidth, rulerHeight, viewport()->width() - headerWidth,
                        viewport()->height() - rulerHeight));
    if (s)
        for (std::size_t row = 0; row < s->tracks.size(); ++row) {
            const auto &track = s->tracks[row];
            const int y = yAt(row);
            if (y + rowHeight < rulerHeight || y > viewport()->height())
                continue;
            p.fillRect(QRect(headerWidth, y, viewport()->width() - headerWidth, rowHeight - 1),
                       QColor(row % 2 ? "#182330" : "#16202c"));
            for (const auto &clip : track.clips) {
                const auto box = QRectF(xAt(clip.position), y + 6,
                                        std::max(2.0, seconds(clip.source.duration) * pixels_),
                                        rowHeight - 12.0);
                if (box.right() < headerWidth)
                    continue;
                if (box.left() > viewport()->width())
                    break;
                p.setOpacity(track.playback.enabled ? 1.0 : 0.45);
                p.setPen(clip.id == selected_ ? QColor("#70e1d2") : QColor("#42708c"));
                p.setBrush(clip.id == selected_ ? QColor("#267f8e") : QColor("#25465c"));
                p.drawRoundedRect(box, 4, 4);
                QString label = "Clip " + QString::number(clip.id.value);
                for (const auto &asset : project_.media)
                    if (asset.id == clip.media)
                        label = QString::fromStdString(asset.name);
                p.setPen(QColor("#e2f0f4"));
                auto textBox = box.intersected(QRectF(headerWidth, rulerHeight,
                                                      viewport()->width() - headerWidth,
                                                      viewport()->height()));
                p.drawText(textBox.adjusted(8, 0, -6, -44), Qt::AlignVCenter,
                           p.fontMetrics().elidedText(
                               label, Qt::ElideRight,
                               static_cast<int>(std::max(0.0, textBox.width() - 14))));
                if (cache_)
                    for (const auto &asset : project_.media)
                        if (asset.id == clip.media) {
                            const double left =
                                std::max(box.left(), static_cast<double>(headerWidth));
                            const double right =
                                std::min(box.right(), static_cast<double>(viewport()->width()));
                            const double sourceStart = seconds(clip.source.start);
                            const auto video =
                                track.kind == TrackKind::Video
                                    ? select_stream(asset, TrackKind::Video, clip.routing.video)
                                    : std::nullopt;
                            const auto audio =
                                select_stream(asset, TrackKind::Audio, clip.routing.audio);
                            p.save();
                            p.setClipRect(box.adjusted(1, 22, -1, -2), Qt::IntersectClip);
                            if (video) {
                                const double tile = 96;
                                for (double x =
                                         box.left() + std::floor((left - box.left()) / tile) * tile;
                                     x < right; x += tile) {
                                    const auto bucket = RationalTime{
                                        static_cast<std::int64_t>(std::floor(
                                            (sourceStart + (x - box.left()) / pixels_) / 2)) *
                                        2};
                                    const auto time = std::max(clip.source.start, bucket);
                                    if (const auto *item =
                                            cache_->request(asset.id, *video, false, time);
                                        item && !item->image.isNull())
                                        p.drawImage(QRectF(x + 2, box.top() + 23, 85, 48),
                                                    item->image);
                                }
                            }
                            if (audio) {
                                const double begin = sourceStart + (left - box.left()) / pixels_;
                                const double finish = sourceStart + (right - box.left()) / pixels_;
                                const double middle = box.top() + (video ? 59 : 49);
                                const double amplitude = video ? 12 : 22;
                                p.setPen(QColor(track.playback.muted ? "#6c8494" : "#a5efc9"));
                                for (auto bucket = static_cast<std::int64_t>(begin / 10) * 10;
                                     static_cast<double>(bucket) < finish; bucket += 10) {
                                    const auto *item = cache_->request(asset.id, *audio, true,
                                                                       RationalTime{bucket});
                                    if (!item || item->peaks.empty())
                                        continue;
                                    const double partLeft =
                                        std::max(left, box.left() + (static_cast<double>(bucket) -
                                                                     sourceStart) *
                                                                        pixels_);
                                    const double partRight = std::min(
                                        right, box.left() + (static_cast<double>(bucket + 10) -
                                                             sourceStart) *
                                                                pixels_);
                                    for (double x = partLeft; x < partRight; x += 2) {
                                        const double at = sourceStart + (x - box.left()) / pixels_ -
                                                          static_cast<double>(bucket);
                                        const auto first = static_cast<std::size_t>(
                                            std::max(0.0, std::floor(at * 100)));
                                        const auto last = std::min(
                                            item->peaks.size(),
                                            static_cast<std::size_t>(std::max(
                                                0.0, std::ceil((at + 2 / pixels_) * 100))));
                                        float peak = 0;
                                        for (auto i = first; i < last; ++i)
                                            peak = std::max(peak, item->peaks[i]);
                                        const double height =
                                            std::min(1.0,
                                                     peak * (track.playback.gain_milli / 1000.0)) *
                                            amplitude;
                                        p.drawLine(QPointF(x, middle - height),
                                                   QPointF(x, middle + height));
                                    }
                                }
                            }
                            p.restore();
                            break;
                        }
                if (clip.id == selected_) {
                    p.fillRect(QRectF(box.left() + 2, box.top() + 8, 3, box.height() - 16),
                               QColor("#9ef3e5"));
                    p.fillRect(QRectF(box.right() - 5, box.top() + 8, 3, box.height() - 16),
                               QColor("#9ef3e5"));
                }
            }
        }
    p.setOpacity(1);
    if (ghost_ && s)
        for (std::size_t row = 0; row < s->tracks.size(); ++row)
            if (s->tracks[row].id == ghostTrack_) {
                p.setPen(QPen(error_.isEmpty() ? QColor("#87f5d5") : QColor("#ff8077"), 2,
                              Qt::DashLine));
                p.setBrush(QColor(70, 140, 130, 100));
                p.drawRect(QRectF(xAt(ghost_->position), yAt(row) + 5,
                                  std::max(2.0, seconds(ghost_->source.duration) * pixels_),
                                  rowHeight - 10.0));
            }
    p.restore();
    p.fillRect(QRect(0, 0, viewport()->width(), rulerHeight), QColor("#1c2a38"));
    p.setPen(QColor("#a2b4c6"));
    double interval = 1;
    while (interval * pixels_ < 72)
        interval *= 2;
    while (interval * pixels_ > 180 && interval > 0.125)
        interval /= 2;
    const double start = horizontalScrollBar()->value() / pixels_;
    for (double t = std::ceil(start / interval) * interval;
         xAt(fromSeconds(t)) < viewport()->width() && t <= 86400; t += interval) {
        const double x = headerWidth + t * pixels_ - horizontalScrollBar()->value();
        p.drawText(QPointF(x + 4, 19), QString::number(t, 'f', interval < 1 ? 2 : 0) + "s");
        p.drawLine(QPointF(x, 24), QPointF(x, rulerHeight));
    }
    p.drawText(QRect(8, 0, headerWidth - 16, rulerHeight), Qt::AlignVCenter,
               snapping_ ? "TRACKS  |  Snap on" : "TRACKS  |  Snap off");
    const double head = xAt(position_);
    if (head >= headerWidth && head <= viewport()->width()) {
        p.setPen(QPen(QColor("#f4bc72"), 2));
        p.drawLine(QPointF(head, 22), QPointF(head, viewport()->height()));
    }
    if (s && s->tracks.empty())
        p.drawText(viewport()->rect().adjusted(headerWidth, rulerHeight, 0, 0), Qt::AlignCenter,
                   "Place media on a track to begin");
    if (!error_.isEmpty()) {
        p.setPen(QColor("#ffb4aa"));
        p.drawText(QRect(headerWidth + 8, viewport()->height() - 25,
                         viewport()->width() - headerWidth - 16, 24),
                   Qt::AlignVCenter, error_);
    }
}
void Timeline::cancelGesture() {
    preview_.reset();
    command_.reset();
    ghost_.reset();
    insertion_.reset();
    error_.clear();
    moved_ = inserting_ = false;
    viewport()->update();
}
void Timeline::previewCommand(const Command &command, TrackId track) {
    command_.reset();
    try {
        auto transaction = preview_->begin();
        const auto result = transaction.execute(command);
        const auto candidate = transaction.preview();
        const auto id = inserting_ ? result.clip.value_or(ClipId{}) : initial_.id;
        for (const auto &seq : candidate.sequences)
            for (const auto &t : seq.tracks)
                for (const auto &c : t.clips)
                    if (c.id == id) {
                        ghost_ = c;
                        ghostTrack_ = t.id;
                    }
        command_ = command;
        error_.clear();
    } catch (const std::exception &e) {
        error_ = QString::fromUtf8(e.what());
        ghostTrack_ = track;
    }
    viewport()->update();
}
void Timeline::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton)
        return;
    setFocus();
    cancelGesture();
    if (event->position().x() < headerWidth)
        return;
    const auto point = event->position();
    const auto *track = trackAt(point.y());
    if (track)
        for (const auto &clip : track->clips)
            if (clipRect(clip.id).contains(point)) {
                initial_ = clip;
                initialTrack_ = track->id;
                press_ = point;
                const auto box = clipRect(clip.id);
                gesture_ = point.x() <= box.left() + 7    ? ClipGesture::TrimLeft
                           : point.x() >= box.right() - 7 ? ClipGesture::TrimRight
                                                          : ClipGesture::Move;
                const auto at = timeAt(point.x());
                grab_ = at >= clip.position ? at - clip.position : RationalTime{};
                preview_ = std::make_unique<Editor>(project_, HistoryLimits{0, 0});
                selected_ = clip.id;
                emit selected(clip.id);
                viewport()->update();
                return;
            }
    emit sought(timeAt(point.x()));
}
void Timeline::updateDrag(QPointF point) {
    if (!preview_)
        return;
    if (!moved_ && (point - press_).manhattanLength() < QApplication::startDragDistance())
        return;
    if (!moved_) {
        moved_ = true;
        emit gestureStarted();
    }
    if (point.x() > viewport()->width() - 16)
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + 12);
    if (point.x() < headerWidth + 16)
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - 12);
    const auto *track = trackAt(point.y());
    command_.reset();
    try {
        if (!track || point.x() < headerWidth)
            throw DomainError("Drop on an existing track");
        auto target = timeAt(point.x());
        if (gesture_ == ClipGesture::Move)
            target = target >= grab_ ? target - grab_ : RationalTime{};
        const auto command =
            timeline_drag(project_, sequence_, initial_.id, track->id, gesture_, target, snapping_,
                          position_, fromSeconds(8.0 / pixels_));
        ghost_ = initial_;
        if (const auto *move = std::get_if<MoveClip>(&command))
            ghost_->position = move->position;
        if (const auto *trim = std::get_if<TrimClip>(&command)) {
            ghost_->position = trim->position;
            ghost_->source = trim->source;
        }
        ghostTrack_ = track->id;
        previewCommand(command, track->id);
    } catch (const std::exception &e) {
        error_ = QString::fromUtf8(e.what());
        viewport()->update();
    }
}
void Timeline::mouseMoveEvent(QMouseEvent *event) {
    if (preview_ && (event->buttons() & Qt::LeftButton)) {
        updateDrag(event->position());
        return;
    }
    viewport()->setCursor(Qt::ArrowCursor);
    if (const auto *track = trackAt(event->position().y()))
        for (const auto &clip : track->clips) {
            const auto box = clipRect(clip.id);
            if (box.contains(event->position())) {
                viewport()->setCursor(event->position().x() <= box.left() + 7 ||
                                              event->position().x() >= box.right() - 7
                                          ? Qt::SizeHorCursor
                                          : Qt::OpenHandCursor);
                break;
            }
        }
}
void Timeline::finishGesture() {
    const auto command = command_;
    const auto revision = project_.revision;
    const auto error = error_;
    const bool changed =
        inserting_ || (ghost_ && (*ghost_ != initial_ || ghostTrack_ != initialTrack_));
    cancelGesture();
    if (!error.isEmpty())
        emit rejected(error);
    else if (command && changed)
        emit editRequested(*command, revision, "Timeline gesture");
}
void Timeline::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || !preview_)
        return;
    if (moved_) {
        updateDrag(event->position());
        finishGesture();
    } else {
        cancelGesture();
        emit sought(timeAt(event->position().x()));
    }
}
void Timeline::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        cancelGesture();
        event->accept();
        return;
    }
    if (preview_) {
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_BracketLeft || event->key() == Qt::Key_BracketRight) {
        std::vector<ClipId> clips;
        if (const auto *s = sequence())
            for (const auto &track : s->tracks)
                for (const auto &clip : track.clips)
                    clips.push_back(clip.id);
        if (!clips.empty()) {
            auto at = std::find(clips.begin(), clips.end(), selected_);
            if (at == clips.end())
                at = clips.begin();
            else if (event->key() == Qt::Key_BracketRight && at + 1 != clips.end())
                ++at;
            else if (event->key() == Qt::Key_BracketLeft && at != clips.begin())
                --at;
            selected_ = *at;
            auto box = clipRect(selected_);
            if (box.left() < headerWidth)
                horizontalScrollBar()->setValue(horizontalScrollBar()->value() +
                                                static_cast<int>(box.left()) - headerWidth);
            else if (box.right() > viewport()->width())
                horizontalScrollBar()->setValue(horizontalScrollBar()->value() +
                                                static_cast<int>(box.left()) - headerWidth);
            if (box.top() < rulerHeight)
                verticalScrollBar()->setValue(verticalScrollBar()->value() +
                                              static_cast<int>(box.top()) - rulerHeight);
            else if (box.bottom() > viewport()->height())
                verticalScrollBar()->setValue(verticalScrollBar()->value() +
                                              static_cast<int>(box.bottom()) -
                                              viewport()->height());
            emit selected(selected_);
            viewport()->update();
        }
    } else if (event->key() == Qt::Key_Delete && selected_.value)
        emit editRequested(DeleteClip{selected_}, project_.revision, "Delete clip");
    else if (event->key() == Qt::Key_S && selected_.value)
        emit editRequested(SplitClip{selected_, position_}, project_.revision, "Split clip");
    else if (event->key() == Qt::Key_Space)
        emit playRequested();
    else if (event->key() == Qt::Key_Left)
        emit stepRequested(-1);
    else if (event->key() == Qt::Key_Right)
        emit stepRequested(1);
    else if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal)
        setZoom(pixels_ * 1.25);
    else if (event->key() == Qt::Key_Minus)
        setZoom(pixels_ / 1.25);
    else if (event->key() == Qt::Key_Home) {
        horizontalScrollBar()->setValue(0);
        emit sought({});
    } else {
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }
    event->accept();
}
void Timeline::wheelEvent(QWheelEvent *event) {
    if (event->modifiers() & Qt::ControlModifier)
        zoomAt(pixels_ * std::pow(1.25, event->angleDelta().y() / 120.0), event->position().x());
    else if (event->modifiers() & Qt::ShiftModifier)
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - event->angleDelta().y());
    else {
        QAbstractScrollArea::wheelEvent(event);
        return;
    }
    event->accept();
}
void Timeline::dragEnterEvent(QDragEnterEvent *event) {
    if (!event->mimeData()->hasFormat(mime))
        return;
    cancelGesture();
    try {
        const auto bytes = event->mimeData()->data(mime);
        if (bytes.size() > 4096)
            throw DomainError("Invalid media drag");
        const auto payload = QJsonDocument::fromJson(bytes).object();
        if (payload["project"].toString().toULongLong() != project_.id.value ||
            payload["revision"].toString().toULongLong() != project_.revision)
            throw DomainError("Project changed; start the media drag again");
        const MediaId asset{payload["media"].toString().toULongLong()};
        const RationalTime in{payload["in_value"].toString().toLongLong(),
                              payload["in_rate"].toString().toLongLong()};
        const RationalTime duration{payload["duration_value"].toString().toLongLong(),
                                    payload["duration_rate"].toString().toLongLong()};
        insertion_ = InsertClip{{}, asset, {}, {in, duration}};
        preview_ = std::make_unique<Editor>(project_, HistoryLimits{0, 0});
        inserting_ = moved_ = true;
        emit gestureStarted();
        event->acceptProposedAction();
    } catch (const std::exception &e) {
        emit rejected(QString::fromUtf8(e.what()));
    }
}
void Timeline::updateDrop(QPointF point) {
    if (!preview_ || !insertion_)
        return;
    const auto *track = trackAt(point.y());
    if (!track || point.x() < headerWidth) {
        command_.reset();
        error_ = "Drop on an existing track";
        viewport()->update();
        return;
    }
    auto command = *insertion_;
    command.track = track->id;
    command.position = timeAt(point.x());
    command.routing.video.mode =
        track->kind == TrackKind::Audio ? StreamMode::Disabled : StreamMode::Automatic;
    if (snapping_) {
        std::vector<RationalTime> edges;
        if (const auto *s = sequence()) {
            for (const auto &t : s->tracks)
                for (const auto &c : t.clips) {
                    edges.push_back(c.position);
                    edges.push_back(c.position + c.source.duration);
                }
            command.position = snap_time(command.position, s->frame_duration, position_, edges,
                                         fromSeconds(8.0 / pixels_));
        }
    }
    ghost_ = Clip{{}, command.media, command.position, command.source, command.routing};
    ghostTrack_ = track->id;
    previewCommand(command, track->id);
}
void Timeline::dragMoveEvent(QDragMoveEvent *event) {
    try {
        updateDrop(event->position());
    } catch (const std::exception &e) {
        command_.reset();
        error_ = QString::fromUtf8(e.what());
    }
    if (command_)
        event->acceptProposedAction();
    else
        event->ignore();
}
void Timeline::dragLeaveEvent(QDragLeaveEvent *event) {
    cancelGesture();
    event->accept();
}
void Timeline::dropEvent(QDropEvent *event) {
    try {
        updateDrop(event->position());
    } catch (const std::exception &e) {
        command_.reset();
        error_ = QString::fromUtf8(e.what());
    }
    if (command_)
        event->acceptProposedAction();
    finishGesture();
}
} // namespace nle::desktop
