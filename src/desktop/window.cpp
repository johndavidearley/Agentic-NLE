#include "desktop/window.hpp"
#include "desktop/media_cache.hpp"
#include "project/persistence.hpp"
#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
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
#include <cmath>
#include <set>
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
Window::Window(QString worker, QString ffprobe, bool audible, QString recoveryDirectory)
    : editor_(std::make_unique<Editor>("Untitled")), saved_(editor_->snapshot()),
      recoveryDirectory_(std::move(recoveryDirectory)), ffprobe_(std::move(ffprobe)),
      transport_(worker, audible, this, ffprobe_) {
    auto cacheWorker = QFileInfo(worker).dir().filePath("nle-cache-worker");
#ifdef _WIN32
    cacheWorker += ".exe";
#endif
    mediaCache_ = new MediaCache(cacheWorker, this);
    setWindowTitle("Agentic NLE");
    resize(1360, 900);
    auto *toolbar = addToolBar("Project");
    toolbar->setMovable(false);
    const auto action = [&](const QString &text, const auto &callback) {
        auto *item = toolbar->addAction(text);
        connect(item, &QAction::triggered, this, callback);
    };
    action("New", [this] {
        if (!mayDiscard())
            return;
        try {
            if (document_)
                document_->discard_recovery();
            document_.reset();
            transport_.cancel();
            editor_ = std::make_unique<Editor>("Untitled");
            saved_ = editor_->snapshot();
            sequence_ = {};
            selected_ = {};
            sourceSelected_ = {};
            path_.clear();
            startDraft();
            refresh();
        } catch (const std::exception &error) {
            report(error.what());
        }
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
    action("Save As…", [this] {
        const auto file =
            QFileDialog::getSaveFileName(this, "Save project copy", path_, "NLE projects (*.nle)");
        if (!file.isEmpty())
            try {
                saveProject(file);
            } catch (const std::exception &error) {
                report(error.what());
            }
    });
    action("Recover draft…", [this] {
        if (mayDiscard() && !restoreDrafts())
            report("No available unsaved draft was found.");
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
        if (!file.isEmpty()) {
            ffprobe_ = file;
            transport_.setProbeTool(file);
        }
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
    library_ = new MediaLibrary;
    library_->setDragEnabled(true);
    library_->payload = [this] { return mediaDragPayload(); };
    library_->setObjectName("mediaLibrary");
    leftLayout->addWidget(library_);
    auto *sourceRange = new QFormLayout;
    sourceIn_ = new QLineEdit("0");
    sourceOut_ = new QLineEdit;
    sourceIn_->setObjectName("sourceSelectionIn");
    sourceOut_->setObjectName("sourceSelectionOut");
    sourceRange->addRow("Source in (s)", sourceIn_);
    sourceRange->addRow("Source out (s)", sourceOut_);
    leftLayout->addLayout(sourceRange);
    connect(library_, &QListWidget::currentItemChanged, this, [this] { refreshSourceSelection(); });
    auto *append = button("Append to timeline", "appendButton");
    leftLayout->addWidget(append);
    connect(append, &QPushButton::clicked, this, &Window::appendSelected);
    auto *place = button("Place on track...", "placeButton");
    leftLayout->addWidget(place);
    connect(place, &QPushButton::clicked, this, &Window::placeSelected);
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
    auto *previousFrame = button("Previous frame", "previousFrameButton");
    auto *nextFrame = button("Next frame", "nextFrameButton");
    controls->addWidget(previousFrame);
    controls->addWidget(nextFrame);
    connect(previousFrame, &QPushButton::clicked, this, [this] { transport_.step(-1); });
    connect(nextFrame, &QPushButton::clicked, this, [this] { transport_.step(1); });
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
    auto *settings = button("Track / routing...", "playbackSettingsButton");
    form->addRow(settings);
    connect(settings, &QPushButton::clicked, this, &Window::playbackSettings);
    splitter->addWidget(right);
    splitter->setSizes({240, 760, 280});
    auto *timelineHeading = new QHBoxLayout;
    timelineHeading->addWidget(new QLabel("TIMELINE"));
    sequences_ = new QComboBox;
    timelineHeading->addWidget(sequences_);
    auto *output = button("Sequence settings...", "sequenceSettingsButton");
    timelineHeading->addWidget(output);
    connect(output, &QPushButton::clicked, this, &Window::sequenceSettings);
    timelineHeading->addStretch();
    auto *snap = new QCheckBox("Snap");
    snap->setObjectName("snapToggle");
    snap->setChecked(true);
    snap->setToolTip("Snap to output frames, playhead and clip edges within 8 pixels");
    timelineHeading->addWidget(snap);
    timelineHeading->addWidget(new QLabel("Zoom"));
    auto *zoom = new QSlider(Qt::Horizontal);
    zoom->setObjectName("timelineZoom");
    zoom->setRange(0, 100);
    zoom->setValue(58);
    zoom->setFixedWidth(150);
    zoom->setAccessibleName("Timeline zoom");
    timelineHeading->addWidget(zoom);
    layout->addLayout(timelineHeading);
    connect(sequences_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) {
            sequence_ = {sequences_->itemData(index).toULongLong()};
            selected_ = {};
            refresh();
        }
    });
    timeline_ = new Timeline;
    timeline_->setCache(mediaCache_);
    connect(mediaCache_, &MediaCache::ready, timeline_->viewport(), qOverload<>(&QWidget::update));
    auto *cacheRefresh = new QTimer(this);
    cacheRefresh->setInterval(1000);
    connect(cacheRefresh, &QTimer::timeout, timeline_->viewport(), qOverload<>(&QWidget::update));
    cacheRefresh->start();
    layout->addWidget(timeline_, 1);
    connect(snap, &QCheckBox::toggled, timeline_, &Timeline::setSnapping);
    connect(zoom, &QSlider::valueChanged, this,
            [this](int value) { timeline_->setZoom(2 * std::pow(600.0, value / 100.0)); });
    connect(timeline_, &Timeline::zoomChanged, this, [zoom](double value) {
        QSignalBlocker block(zoom);
        zoom->setValue(static_cast<int>(std::lround(100 * std::log(value / 2) / std::log(600.0))));
    });
    connect(timeline_, &Timeline::editRequested, this, &Window::editAtRevision);
    connect(timeline_, &Timeline::rejected, this, &Window::report);
    connect(timeline_, &Timeline::gestureStarted, this, [this] {
        if (transport_.playing())
            transport_.pause();
    });
    connect(timeline_, &Timeline::playRequested, play_, &QPushButton::click);
    connect(timeline_, &Timeline::stepRequested, this,
            [this](int direction) { transport_.step(direction); });
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
    try {
        startDraft();
    } catch (const std::exception &error) {
        report(error.what());
    }
    recoveryTimer_.setInterval(30000);
    connect(&recoveryTimer_, &QTimer::timeout, this, [this] { (void)checkpointRecovery(); });
    recoveryTimer_.start();
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
    try {
        if (document_)
            document_->discard_recovery();
    } catch (const std::exception &error) {
        report(error.what());
        event->ignore();
        return;
    }
    recoveryTimer_.stop();
    document_.reset();
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
    const QSignalBlocker librarySignals(library_);
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
    refreshSourceSelection();
    undo_->setEnabled(editor_->can_undo());
    redo_->setEnabled(editor_->can_redo());
    cancelImport_->setEnabled(watcher_.isRunning());
    refreshInspector();
    mediaCache_->synchronize(project);
    timeline_->display(project, sequence_, selected_);
    try {
        auto plan = sequence_.value ? playback::make_sequence_plan(project, sequence_)
                                    : playback::SequencePlan{};
        scrub_->setRange(0, static_cast<int>(playback::milliseconds(plan.duration)));
        if (sequence_.value)
            transport_.open(project, sequence_);
        else
            transport_.open(playback::Plan{});
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
void Window::refreshSourceSelection() {
    if (!library_->currentItem())
        return;
    const MediaId id{library_->currentItem()->data(Qt::UserRole).toULongLong()};
    if (sourceSelected_ == id)
        return;
    sourceSelected_ = id;
    for (const auto &asset : editor_->snapshot().media)
        if (asset.id == id) {
            sourceIn_->setText("0");
            sourceOut_->setText(timeText(asset.duration));
            return;
        }
}
TimeRange Window::selectedSourceRange(const MediaAsset &asset) const {
    const auto in = parseTime(sourceIn_->text()), out = parseTime(sourceOut_->text());
    if (out <= in || out > asset.duration)
        throw DomainError("Source out must follow source in and stay within the media duration.");
    return {in, out - in};
}
QByteArray Window::mediaDragPayload() {
    try {
        if (!library_->currentItem())
            return {};
        const auto project = editor_->snapshot();
        const MediaId id{library_->currentItem()->data(Qt::UserRole).toULongLong()};
        for (const auto &asset : project.media)
            if (asset.id == id) {
                const auto range = selectedSourceRange(asset);
                return QJsonDocument(
                           QJsonObject{{"project", QString::number(project.id.value)},
                                       {"revision", QString::number(project.revision)},
                                       {"media", QString::number(id.value)},
                                       {"in_value", QString::number(range.start.value())},
                                       {"in_rate", QString::number(range.start.rate())},
                                       {"duration_value", QString::number(range.duration.value())},
                                       {"duration_rate", QString::number(range.duration.rate())}})
                    .toJson(QJsonDocument::Compact);
            }
    } catch (const std::exception &e) {
        report(e.what());
    }
    return {};
}
void Window::editAtRevision(const Command &command, std::uint64_t revision, const QString &label) {
    try {
        transport_.cancel();
        const auto result = editor_->execute(
            command, {{{"human:desktop"}, ActorKind::Human}, label.toStdString(), revision});
        if (result.clip)
            selected_ = *result.clip;
        refresh();
    } catch (const std::exception &e) {
        report(e.what());
    }
}
void Window::startDraft() {
    if (recoveryDirectory_.isEmpty())
        return;
    if (!QDir().mkpath(recoveryDirectory_))
        throw DomainError(
            "Cannot create the draft recovery folder. Save the project to enable recovery.");
    const auto anchor =
        QDir(recoveryDirectory_).filePath("draft-" + QString::number(saved_.id.value) + ".nle");
    document_ = std::make_unique<DocumentFile>(media::utf8_path(anchor.toStdString()), true, true);
}
bool Window::checkpointRecovery() {
    const auto value = editor_->snapshot();
    if (value == saved_)
        return true;
    try {
        if (!document_)
            throw DomainError("Save this project once to enable recovery.");
        document_->checkpoint(value);
        return true;
    } catch (const std::exception &error) {
        report(QString("Recovery checkpoint failed: ") + error.what());
        return false;
    }
}
bool Window::restoreDrafts(RecoveryChoice choice) {
    if (recoveryDirectory_.isEmpty())
        return false;
    const auto files =
        QDir(recoveryDirectory_)
            .entryList({"*.nle.recovery-0", "*.nle.recovery-1"}, QDir::Files, QDir::Time);
    std::set<QString> visited;
    for (const auto &file : files) {
        const auto anchor = QDir(recoveryDirectory_).filePath(file.left(file.size() - 11));
        if (!visited.insert(anchor).second ||
            (document_ && document_->matches(media::utf8_path(anchor.toStdString()))))
            continue;
        try {
            openDocument(anchor, choice, true);
            return true;
        } catch (const FileError &error) {
            if (error.code == "open_cancelled")
                return false;
            if (error.code != "project_locked")
                report(error.what());
        } catch (const std::exception &error) {
            report(error.what());
        }
    }
    return false;
}
void Window::openProject(const QString &path, RecoveryChoice choice) {
    openDocument(path, choice, false);
}
void Window::openDocument(const QString &path, RecoveryChoice choice, bool draft) {
    const auto native = media::utf8_path(path.toStdString());
    std::unique_ptr<DocumentFile> next;
    auto *file = document_.get();
    if (!file || !file->matches(native)) {
        next = std::make_unique<DocumentFile>(native, true, draft);
        file = next.get();
    } else if (file->exists() && load_project(native) != file->load()) {
        throw FileError("save_conflict", "The saved file changed outside this editor. Save your "
                                         "work to another path before reopening it.");
    }
    const auto baseline = file->exists() ? file->load() : Editor("Untitled").snapshot();
    auto loaded = baseline;
    const auto pending = file->recovery();
    if (draft && !pending.project)
        throw FileError("recovery_unavailable", "No valid checkpoint is available for this draft.");
    if (pending.project) {
        if (choice == RecoveryChoice::Ask) {
            QMessageBox dialog(QMessageBox::Question, "Recover unsaved work",
                               "A recovery checkpoint is available for " +
                                   QString::fromStdString(pending.project->name) +
                                   ". Recover it or discard it and use the saved version?" +
                                   (pending.warning.empty()
                                        ? QString{}
                                        : "\n" + QString::fromStdString(pending.warning)),
                               QMessageBox::NoButton, this);
            auto *recover = dialog.addButton("Recover", QMessageBox::AcceptRole);
            auto *discard = dialog.addButton("Discard recovery", QMessageBox::DestructiveRole);
            dialog.addButton(QMessageBox::Cancel);
            dialog.exec();
            if (dialog.clickedButton() == recover)
                choice = RecoveryChoice::Recover;
            else if (dialog.clickedButton() == discard)
                choice = RecoveryChoice::Discard;
            else
                throw FileError("open_cancelled", "Project opening was cancelled.");
        }
        if (choice == RecoveryChoice::Recover)
            loaded = file->recover();
        else
            file->discard_recovery();
    }
    if (!pending.project && !pending.warning.empty()) {
        if (choice == RecoveryChoice::Ask) {
            const auto decision = QMessageBox::warning(
                this, "Recovery unavailable",
                QString::fromStdString(pending.warning) +
                    "\nDiscard these checkpoints and open the saved project?",
                QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
            if (decision != QMessageBox::Discard)
                throw FileError("open_cancelled", "Project opening was cancelled.");
            choice = RecoveryChoice::Discard;
        }
        if (choice == RecoveryChoice::Recover)
            throw FileError("recovery_unavailable",
                            "No matching valid recovery checkpoint is available.");
        file->discard_recovery();
    }
    auto replacement = std::make_unique<Editor>(loaded);
    if (next && document_)
        document_->discard_recovery();
    transport_.cancel();
    if (next)
        document_ = std::move(next);
    editor_ = std::move(replacement);
    saved_ = baseline;
    path_ = draft ? QString{} : path;
    sequence_ = {};
    selected_ = {};
    sourceSelected_ = {};
    refresh();
    if (pending.project && choice == RecoveryChoice::Recover)
        report("Recovered unsaved work. Save the project to keep this version.");
    else if (!pending.warning.empty())
        report(QString::fromStdString(pending.warning));
}
void Window::saveProject(const QString &path) {
    const auto value = editor_->snapshot();
    const auto native = media::utf8_path(path.toStdString());
    std::string warning;
    if (document_ && document_->matches(native))
        document_->save(value);
    else {
        auto next = std::make_unique<DocumentFile>(native, true, true);
        next->save(value);
        if (document_)
            try {
                document_->discard_recovery();
            } catch (const std::exception &) {
                warning = " An old recovery checkpoint could not be removed.";
            }
        document_ = std::move(next);
    }
    warning += document_->cleanup_warning();
    saved_ = value;
    path_ = path;
    setWindowTitle(QString::fromStdString(value.name) + " — Agentic NLE");
    report("Project saved" + QString::fromStdString(warning));
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
            *batch.execute(InsertClip{track, id, position, selectedSourceRange(*asset)}).clip;
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
