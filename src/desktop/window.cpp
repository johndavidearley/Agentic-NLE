#include "desktop/window.hpp"
#include "project/persistence.hpp"
#include <QCloseEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QToolBar>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <algorithm>
#include <charconv>
namespace nle::desktop {
namespace {
double seconds(RationalTime t) {
    return static_cast<double>(t.value()) / static_cast<double>(t.rate());
}
QString timeText(RationalTime t) {
    return t.rate() == 1 ? QString::number(t.value())
                         : QString::number(t.value()) + "/" + QString::number(t.rate());
}
RationalTime parseTime(QString text) {
    const auto raw = text.trimmed().toStdString();
    const auto integer = [](const std::string &value) {
        std::int64_t result{};
        const auto [end, error] =
            std::from_chars(value.data(), value.data() + value.size(), result);
        if (error != std::errc{} || end != value.data() + value.size())
            throw DomainError("Enter seconds as a decimal or a fraction such as 1001/30000.");
        return result;
    };
    const auto slash = raw.find('/'), dot = raw.find('.');
    if (slash != std::string::npos)
        return {integer(raw.substr(0, slash)), integer(raw.substr(slash + 1))};
    if (dot != std::string::npos) {
        const auto places = raw.size() - dot - 1;
        if (places > 9 || places == 0)
            throw DomainError("Use at most nine decimal places.");
        auto digits = raw;
        digits.erase(dot, 1);
        std::int64_t rate = 1;
        for (std::size_t i = 0; i < places; ++i)
            rate *= 10;
        return {integer(digits), rate};
    }
    return {integer(raw)};
}
QPushButton *button(const QString &text, const QString &name) {
    auto *value = new QPushButton(text);
    value->setObjectName(name);
    return value;
}
} // namespace
void Timeline::display(ProjectSnapshot project, SequenceId sequence, ClipId selected) {
    project_ = std::move(project);
    sequence_ = sequence;
    selected_ = selected;
    update();
}
double Timeline::scale() const {
    double duration = 10;
    for (const auto &sequence : project_.sequences)
        if (sequence.id == sequence_)
            for (const auto &track : sequence.tracks)
                for (const auto &clip : track.clips)
                    duration =
                        std::max(duration, seconds(clip.position + clip.source.duration) + 1);
    return static_cast<double>(std::max(1, width() - 120)) / duration;
}
void Timeline::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#141b25"));
    const auto pixels = scale();
    painter.setPen(QColor("#8b9caf"));
    for (int tick = 0; tick <= 10; ++tick) {
        const double x =
            100 + static_cast<double>(tick) * static_cast<double>(std::max(1, width() - 120)) / 10;
        painter.drawText(QPointF(std::min(x + 4, static_cast<double>(width() - 48)), 20),
                         QString::number((x - 100) / pixels, 'f', 1) + "s");
        painter.setPen(QColor("#273343"));
        painter.drawLine(QPointF(x, 28), QPointF(x, height()));
        painter.setPen(QColor("#8b9caf"));
    }
    int row = 0;
    for (const auto &sequence : project_.sequences)
        if (sequence.id == sequence_)
            for (const auto &track : sequence.tracks) {
                const int y = 38 + row++ * 58;
                painter.setPen(QColor("#cbd5e1"));
                painter.drawText(QRect(10, y, 85, 40), Qt::AlignVCenter,
                                 QString::fromStdString(track.name));
                for (const auto &clip : track.clips) {
                    const QRectF block(100 + seconds(clip.position) * pixels, y,
                                       std::max(2.0, seconds(clip.source.duration) * pixels), 42);
                    painter.setBrush(clip.id == selected_ ? QColor("#267f8e") : QColor("#25465c"));
                    painter.setPen(clip.id == selected_ ? QColor("#70e1d2") : QColor("#42708c"));
                    painter.drawRoundedRect(block, 5, 5);
                    QString name = "Clip " + QString::number(clip.id.value);
                    for (const auto &asset : project_.media)
                        if (asset.id == clip.media)
                            name = QString::fromStdString(asset.name);
                    painter.setPen(QColor("#e2f0f4"));
                    painter.drawText(block.adjusted(8, 0, -5, 0), Qt::AlignVCenter,
                                     painter.fontMetrics().elidedText(
                                         name, Qt::ElideRight,
                                         static_cast<int>(std::max(0.0, block.width() - 12))));
                }
            }
    if (row == 0) {
        painter.setPen(QColor("#7c8da2"));
        painter.drawText(rect(), Qt::AlignCenter, "Import media, then append it to begin editing");
    }
    const double x = 100 + seconds(position_) * pixels;
    painter.setPen(QPen(QColor("#f4bc72"), 2));
    painter.drawLine(QPointF(x, 26), QPointF(x, height()));
}
void Timeline::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || event->position().x() < 100)
        return;
    const auto time =
        RationalTime{static_cast<std::int64_t>(
                         std::clamp((event->position().x() - 100) / scale(), 0.0, 86400.0) * 1000),
                     1000};
    int row = 0;
    for (const auto &sequence : project_.sequences)
        if (sequence.id == sequence_)
            for (const auto &track : sequence.tracks) {
                const int y = 38 + row++ * 58;
                if (event->position().y() >= y && event->position().y() <= y + 42)
                    for (const auto &clip : track.clips)
                        if (time >= clip.position && time < clip.position + clip.source.duration) {
                            emit selected(clip.id);
                            break;
                        }
            }
    emit sought(time);
}
Window::Window(QString worker, QString ffprobe, bool audible)
    : editor_(std::make_unique<Editor>("Untitled")), saved_(editor_->snapshot()),
      ffprobe_(std::move(ffprobe)), transport_(std::move(worker), audible, this) {
    setWindowTitle("Agentic NLE");
    resize(1360, 850);
    auto *toolbar = addToolBar("Project");
    toolbar->setMovable(false);
    const auto action = [&](const QString &text, const auto &callback) {
        auto *item = toolbar->addAction(text);
        connect(item, &QAction::triggered, this, callback);
    };
    action("New", [this] {
        if (!mayDiscard())
            return;
        transport_.cancel();
        editor_ = std::make_unique<Editor>("Untitled");
        saved_ = editor_->snapshot();
        sequence_ = {};
        selected_ = {};
        path_.clear();
        refresh();
    });
    action("Open…", [this] {
        if (!mayDiscard())
            return;
        const auto file =
            QFileDialog::getOpenFileName(this, "Open project", {}, "NLE projects (*.nle)");
        if (!file.isEmpty())
            try {
                openProject(file);
            } catch (const std::exception &e) {
                report(e.what());
            }
    });
    action("Save", [this] {
        auto file = path_;
        if (file.isEmpty())
            file = QFileDialog::getSaveFileName(this, "Save project", {}, "NLE projects (*.nle)");
        if (!file.isEmpty())
            try {
                saveProject(file);
            } catch (const std::exception &e) {
                report(e.what());
            }
    });
    toolbar->addSeparator();
    action("Import media…", [this] {
        const auto file =
            QFileDialog::getOpenFileName(this, "Import media", {},
                                         "Media (*.mp4 *.mov *.mkv *.webm *.avi *.wav *.flac *.mp3 "
                                         "*.ogg *.aiff);;All files (*)");
        if (!file.isEmpty())
            importMedia(file);
    });
    cancelImport_ = button("Cancel import", "cancelImportButton");
    toolbar->addWidget(cancelImport_);
    connect(cancelImport_, &QPushButton::clicked, this, [this] {
        if (importStop_)
            importStop_->store(true);
    });
    action("Probe tool…", [this] {
        const auto file = QFileDialog::getOpenFileName(this, "Choose ffprobe executable");
        if (!file.isEmpty())
            ffprobe_ = file;
    });
    toolbar->addSeparator();
    undo_ = button("Undo", "undoButton");
    redo_ = button("Redo", "redoButton");
    toolbar->addWidget(undo_);
    toolbar->addWidget(redo_);
    connect(undo_, &QPushButton::clicked, this, [this] {
        try {
            transport_.pause();
            editor_->undo({{{"human:desktop"}, ActorKind::Human}, "Undo", editor_->revision()});
            refresh();
        } catch (const std::exception &e) {
            report(e.what());
        }
    });
    connect(redo_, &QPushButton::clicked, this, [this] {
        try {
            transport_.pause();
            editor_->redo({{{"human:desktop"}, ActorKind::Human}, "Redo", editor_->revision()});
            refresh();
        } catch (const std::exception &e) {
            report(e.what());
        }
    });
    auto *root = new QWidget;
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(12);
    auto *heading = new QLabel("AGENTIC NLE   /   EDIT");
    heading->setStyleSheet("font-size:18px;font-weight:600;color:#e6edf5;");
    layout->addWidget(heading);
    auto *splitter = new QSplitter;
    layout->addWidget(splitter, 1);
    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->addWidget(new QLabel("MEDIA LIBRARY"));
    library_ = new QListWidget;
    library_->setObjectName("mediaLibrary");
    leftLayout->addWidget(library_);
    auto *append = button("Append to timeline", "appendButton");
    leftLayout->addWidget(append);
    connect(append, &QPushButton::clicked, this, &Window::appendSelected);
    splitter->addWidget(left);
    auto *center = new QWidget;
    auto *centerLayout = new QVBoxLayout(center);
    preview_ = new QLabel("Your preview appears here");
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumSize(480, 280);
    preview_->setStyleSheet(
        "background:#090e15;border:1px solid #293647;border-radius:6px;color:#8190a3;");
    centerLayout->addWidget(preview_, 1);
    auto *controls = new QHBoxLayout;
    play_ = button("Play", "playButton");
    controls->addWidget(play_);
    connect(play_, &QPushButton::clicked, this,
            [this] { transport_.playing() ? transport_.pause() : transport_.play(); });
    auto *stop = button("Stop", "stopButton");
    controls->addWidget(stop);
    connect(stop, &QPushButton::clicked, &transport_, &Transport::cancel);
    clock_ = new QLabel("0.000 / 0.000 s");
    transportInfo_ = new QLabel("Ready");
    controls->addWidget(transportInfo_);
    controls->addStretch();
    controls->addWidget(clock_);
    centerLayout->addLayout(controls);
    scrub_ = new QSlider(Qt::Horizontal);
    scrub_->setObjectName("positionSlider");
    centerLayout->addWidget(scrub_);
    connect(scrub_, &QSlider::sliderReleased, this,
            [this] { transport_.seek({scrub_->value(), 1000}); });
    splitter->addWidget(center);
    auto *right = new QWidget;
    auto *form = new QFormLayout(right);
    form->addRow(new QLabel("CLIP INSPECTOR"));
    sourceInfo_ = new QLabel("Select a clip");
    sourceInfo_->setWordWrap(true);
    form->addRow(sourceInfo_);
    positionEdit_ = new QLineEdit;
    positionEdit_->setObjectName("clipPosition");
    inEdit_ = new QLineEdit;
    inEdit_->setObjectName("clipSourceIn");
    durationEdit_ = new QLineEdit;
    durationEdit_->setObjectName("clipDuration");
    form->addRow("Position (s)", positionEdit_);
    form->addRow("Source in (s)", inEdit_);
    form->addRow("Duration (s)", durationEdit_);
    auto *hint = new QLabel("Decimals or exact fractions, e.g. 1001/30000");
    hint->setWordWrap(true);
    form->addRow(hint);
    auto *apply = button("Apply trim / position", "applyButton");
    form->addRow(apply);
    connect(apply, &QPushButton::clicked, this, [this] {
        try {
            if (selected_.value)
                edit(TrimClip{selected_,
                              parseTime(positionEdit_->text()),
                              {parseTime(inEdit_->text()), parseTime(durationEdit_->text())}},
                     "Trim or position clip");
        } catch (const std::exception &e) {
            report(e.what());
        }
    });
    auto *split = button("Split at playhead", "splitButton");
    form->addRow(split);
    connect(split, &QPushButton::clicked, this, [this] {
        if (selected_.value)
            edit(SplitClip{selected_, transport_.position()}, "Split clip");
    });
    auto *remove = button("Delete clip", "deleteButton");
    form->addRow(remove);
    connect(remove, &QPushButton::clicked, this, [this] {
        if (selected_.value)
            edit(DeleteClip{selected_}, "Delete clip");
    });
    splitter->addWidget(right);
    splitter->setSizes({240, 760, 280});
    auto *timelineHeading = new QHBoxLayout;
    timelineHeading->addWidget(new QLabel("TIMELINE"));
    sequences_ = new QComboBox;
    timelineHeading->addWidget(sequences_);
    timelineHeading->addStretch();
    timelineHeading->addWidget(new QLabel("Click a clip to inspect · click the ruler to seek"));
    layout->addLayout(timelineHeading);
    connect(sequences_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) {
            sequence_ = {sequences_->itemData(index).toULongLong()};
            selected_ = {};
            refresh();
        }
    });
    timeline_ = new Timeline;
    layout->addWidget(timeline_);
    connect(timeline_, &Timeline::selected, this, &Window::selectClip);
    connect(timeline_, &Timeline::sought, this,
            [this](RationalTime time) { transport_.seek(std::min(time, transport_.duration())); });
    notice_ = new QLabel;
    notice_->setWordWrap(true);
    layout->addWidget(notice_);
    setCentralWidget(root);
    connect(&transport_, &Transport::frameReady, this, [this](const QImage &image, qint64, qint64) {
        image_ = image;
        if (image.isNull()) {
            preview_->setPixmap({});
            preview_->setText("Preview paused / no image");
        } else
            preview_->setPixmap(QPixmap::fromImage(image).scaled(
                preview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    });
    connect(&transport_, &Transport::changed, this, [this] {
        play_->setText(transport_.playing() ? "Pause" : "Play");
        clock_->setText(QString::number(seconds(transport_.position()), 'f', 3) + " / " +
                        QString::number(seconds(transport_.duration()), 'f', 3) + " s");
        if (!scrub_->isSliderDown()) {
            QSignalBlocker blocker(scrub_);
            scrub_->setValue(static_cast<int>(playback::milliseconds(transport_.position())));
        }
        timeline_->setPosition(transport_.position());
        transportInfo_->setText(transport_.status());
    });
    auto *undoKey = new QShortcut(QKeySequence::Undo, this);
    connect(undoKey, &QShortcut::activated, undo_, &QPushButton::click);
    auto *redoKey = new QShortcut(QKeySequence::Redo, this);
    connect(redoKey, &QShortcut::activated, redo_, &QPushButton::click);
    refresh();
}
Window::~Window() {
    if (importStop_)
        importStop_->store(true);
}
void Window::report(const QString &message) { notice_->setText(message); }
bool Window::mayDiscard() {
    return editor_->snapshot() == saved_ ||
           QMessageBox::question(this, "Unsaved project", "Discard unsaved changes?",
                                 QMessageBox::Discard | QMessageBox::Cancel,
                                 QMessageBox::Cancel) == QMessageBox::Discard;
}
void Window::closeEvent(QCloseEvent *event) {
    if (!mayDiscard()) {
        event->ignore();
        return;
    }
    if (importStop_)
        importStop_->store(true);
    transport_.cancel();
    event->accept();
}
const Clip *Window::selectedClip(const ProjectSnapshot &project) const {
    for (const auto &sequence : project.sequences)
        for (const auto &track : sequence.tracks)
            for (const auto &clip : track.clips)
                if (clip.id == selected_)
                    return &clip;
    return nullptr;
}
void Window::refreshInspector() {
    const auto project = editor_->snapshot();
    const auto *clip = selectedClip(project);
    if (!clip) {
        selected_ = {};
        sourceInfo_->setText("Select a clip");
        positionEdit_->clear();
        inEdit_->clear();
        durationEdit_->clear();
        return;
    }
    const auto asset = std::find_if(project.media.begin(), project.media.end(),
                                    [&](const auto &value) { return value.id == clip->media; });
    QString details = QString::fromStdString(asset->name);
    if (asset->source)
        for (const auto &stream : asset->source->streams) {
            if (stream.kind == TrackKind::Video)
                details +=
                    "\n" + QString::number(stream.width) + " × " + QString::number(stream.height);
            else
                details += "\n" + QString::number(stream.sample_rate) + " Hz · " +
                           QString::number(stream.channels) + " channels";
        }
    sourceInfo_->setText(details);
    positionEdit_->setText(timeText(clip->position));
    inEdit_->setText(timeText(clip->source.start));
    durationEdit_->setText(timeText(clip->source.duration));
}
void Window::selectClip(ClipId id) {
    selected_ = id;
    refreshInspector();
    timeline_->display(editor_->snapshot(), sequence_, selected_);
}
void Window::refresh() {
    notice_->clear();
    const auto project = editor_->snapshot();
    const auto previous = transport_.position();
    transport_.cancel();
    setWindowTitle(QString::fromStdString(project.name) + (project == saved_ ? "" : " *") +
                   " — Agentic NLE");
    const auto oldMedia =
        library_->currentItem() ? library_->currentItem()->data(Qt::UserRole).toULongLong() : 0;
    library_->clear();
    for (const auto &asset : project.media) {
        auto *item =
            new QListWidgetItem(QString::fromStdString(asset.name) + "\n" +
                                    QString::number(seconds(asset.duration), 'f', 3) + " s · " +
                                    media::status_name(media::source_status(asset)),
                                library_);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(asset.id.value));
        if (asset.id.value == oldMedia)
            library_->setCurrentItem(item);
    }
    if (!library_->currentItem() && library_->count())
        library_->setCurrentRow(library_->count() - 1);
    {
        QSignalBlocker blocker(sequences_);
        sequences_->clear();
        int index = 0, active = 0;
        bool found = false;
        for (const auto &sequence : project.sequences) {
            sequences_->addItem(QString::fromStdString(sequence.name),
                                QVariant::fromValue<qulonglong>(sequence.id.value));
            if (sequence.id == sequence_) {
                active = index;
                found = true;
            }
            ++index;
        }
        if (!found)
            sequence_ = project.sequences.empty() ? SequenceId{} : project.sequences.front().id;
        sequences_->setCurrentIndex(active);
    }
    undo_->setEnabled(editor_->can_undo());
    redo_->setEnabled(editor_->can_redo());
    cancelImport_->setEnabled(watcher_.isRunning());
    refreshInspector();
    timeline_->display(project, sequence_, selected_);
    try {
        auto plan = sequence_.value ? playback::make_plan(project, sequence_) : playback::Plan{};
        scrub_->setRange(0, static_cast<int>(playback::milliseconds(plan.duration)));
        transport_.open(std::move(plan));
        if (previous > RationalTime{})
            transport_.seek(std::min(previous, transport_.duration()));
    } catch (const std::exception &e) {
        transport_.open({});
        scrub_->setRange(0, 0);
        report(e.what());
    }
}
void Window::edit(const Command &command, const std::string &label) {
    try {
        transport_.pause();
        editor_->execute(command,
                         {{{"human:desktop"}, ActorKind::Human}, label, editor_->revision()});
        refresh();
    } catch (const std::exception &e) {
        report(e.what());
    }
}
void Window::openProject(const QString &path) {
    auto loaded = load_project(media::utf8_path(path.toStdString()));
    auto replacement = std::make_unique<Editor>(loaded);
    transport_.cancel();
    editor_ = std::move(replacement);
    saved_ = std::move(loaded);
    path_ = path;
    sequence_ = {};
    selected_ = {};
    refresh();
}
void Window::saveProject(const QString &path) {
    const auto value = editor_->snapshot();
    save_project(value, media::utf8_path(path.toStdString()));
    saved_ = value;
    path_ = path;
    setWindowTitle(QString::fromStdString(value.name) + " — Agentic NLE");
    report("Project saved");
}
void Window::importMedia(const QString &path) {
    if (watcher_.isRunning()) {
        report("An import is already running.");
        return;
    }
    const auto before = editor_->snapshot();
    const auto tool = ffprobe_;
    importStop_ = std::make_shared<std::atomic_bool>(false);
    const auto stop = importStop_;
    watcher_.disconnect(this);
    connect(&watcher_, &QFutureWatcher<ImportResult>::finished, this, [this, before] {
        cancelImport_->setEnabled(false);
        const auto imported = watcher_.result();
        if (!imported.result) {
            report(imported.error);
            return;
        }
        if (editor_->snapshot().id != before.id) {
            report("Project changed; import was discarded.");
            return;
        }
        try {
            editor_->execute(
                imported.result->import_command(),
                {{{"human:desktop"}, ActorKind::Human}, "Import media", before.revision});
            refresh();
            library_->setCurrentRow(library_->count() - 1);
            report("Media imported. Append it to the timeline to preview.");
        } catch (const std::exception &e) {
            report(e.what());
        }
    });
    report("Probing media…");
    cancelImport_->setEnabled(true);
    watcher_.setFuture(QtConcurrent::run([path, tool, stop] {
        try {
            return ImportResult{
                media::probe(media::utf8_path(path.toStdString()),
                             media::utf8_path(tool.toStdString()),
                             {std::chrono::seconds(30), 1024 * 1024, std::cref(*stop)}),
                {}};
        } catch (const std::exception &e) {
            return ImportResult{{}, QString::fromUtf8(e.what())};
        }
    }));
}
void Window::appendSelected() {
    if (!library_->currentItem())
        return;
    try {
        const auto project = editor_->snapshot();
        const MediaId id{library_->currentItem()->data(Qt::UserRole).toULongLong()};
        const auto asset = std::find_if(project.media.begin(), project.media.end(),
                                        [&](const auto &value) { return value.id == id; });
        if (asset == project.media.end())
            throw DomainError("Media not found.");
        auto batch = editor_->begin(
            {{{"human:desktop"}, ActorKind::Human}, "Append media to timeline", project.revision});
        auto sequence = sequence_;
        if (!sequence.value)
            sequence = *batch.execute(CreateSequence{"Main"}).sequence;
        TrackId track{};
        RationalTime position;
        const auto kind = asset->kind == MediaKind::Audio ? TrackKind::Audio : TrackKind::Video;
        for (const auto &value : project.sequences)
            if (value.id == sequence)
                for (const auto &candidate : value.tracks)
                    if (candidate.kind == kind && !track.value) {
                        track = candidate.id;
                        for (const auto &clip : candidate.clips)
                            position = std::max(position, clip.position + clip.source.duration);
                    }
        if (!track.value)
            track =
                *batch.execute(CreateTrack{sequence, kind, kind == TrackKind::Video ? "V1" : "A1"})
                     .track;
        const auto clip =
            *batch.execute(InsertClip{track, id, position, {{}, asset->duration}}).clip;
        editor_->commit(std::move(batch));
        sequence_ = sequence;
        selected_ = clip;
        refresh();
        transport_.seek(position);
    } catch (const std::exception &e) {
        report(e.what());
    }
}
} // namespace nle::desktop
