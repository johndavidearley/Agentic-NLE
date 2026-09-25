#include "desktop/window.hpp"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QPointer>
#include <QSpinBox>
#include <QtConcurrentRun>
#include <algorithm>
namespace nle::desktop {
namespace {
RationalTime time(const QString &text) {
    const auto parts = text.trimmed().split('/');
    bool ok = false;
    const auto value = parts[0].toLongLong(&ok);
    if (!ok)
        throw DomainError("Use whole seconds or an exact fraction, e.g. 1001/30000");
    qint64 rate = 1;
    if (parts.size() == 2) {
        rate = parts[1].toLongLong(&ok);
        if (!ok)
            throw DomainError("Invalid time denominator");
    } else if (parts.size() != 1)
        throw DomainError("Invalid time");
    return {value, rate};
}
QString text(RationalTime value) {
    return QString::number(value.value()) + "/" + QString::number(value.rate());
}
void buttons(QDialog &dialog, QFormLayout &form) {
    auto *b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(b);
    QObject::connect(b, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(b, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
}
} // namespace
void Window::placeSelected() {
    if (!library_->currentItem())
        return;
    try {
        const auto before = editor_->snapshot();
        const MediaId id{library_->currentItem()->data(Qt::UserRole).toULongLong()};
        const auto asset = std::find_if(before.media.begin(), before.media.end(),
                                        [&](const auto &a) { return a.id == id; });
        if (asset == before.media.end())
            return;
        QDialog dialog(this);
        dialog.setWindowTitle("Place media on a track");
        dialog.setObjectName("placeDialog");
        QFormLayout form(&dialog);
        QComboBox target;
        target.setObjectName("placementTrack");
        if (asset->kind != MediaKind::Audio)
            target.addItem("New top video track", -1);
        if (asset->kind != MediaKind::Video)
            target.addItem("New audio track (audio only)", -2);
        for (const auto &sequence : before.sequences)
            if (sequence.id == sequence_)
                for (const auto &track : sequence.tracks)
                    if (supports(asset->kind, track.kind))
                        target.addItem(QString::fromStdString(track.name),
                                       QVariant::fromValue<qulonglong>(track.id.value));
        const auto range = selectedSourceRange(*asset);
        QLineEdit at(text(transport_.position())), source(text(range.start)),
            duration(text(range.duration));
        at.setObjectName("placementPosition");
        source.setObjectName("placementSource");
        duration.setObjectName("placementDuration");
        QCheckBox embedded("Include embedded audio on a video track");
        embedded.setChecked(true);
        embedded.setVisible(asset->kind == MediaKind::AudioVideo);
        form.addRow("Destination", &target);
        form.addRow("Position (seconds / fraction)", &at);
        form.addRow("Source in", &source);
        form.addRow("Duration", &duration);
        form.addRow(&embedded);
        auto *hint = new QLabel("Audio-only placement creates a separate clip. Existing clips keep "
                                "their audio routing.");
        hint->setWordWrap(true);
        form.addRow(hint);
        buttons(dialog, form);
        if (dialog.exec() != QDialog::Accepted)
            return;
        auto batch = editor_->begin(
            {{{"human:desktop"}, ActorKind::Human}, "Place media on track", before.revision});
        auto sequence = sequence_;
        if (!sequence.value)
            sequence = *batch.execute(CreateSequence{"Main"}).sequence;
        const auto choice = target.currentData().toLongLong();
        TrackId track;
        TrackKind kind = choice == -1 ? TrackKind::Video : TrackKind::Audio;
        if (choice < 0) {
            track =
                *batch.execute(CreateTrack{sequence, kind, choice == -1 ? "Video" : "Audio"}).track;
            if (choice == -1)
                (void)batch.execute(ReorderTrack{sequence, track, 0});
        } else {
            track = {static_cast<std::uint64_t>(choice)};
            for (const auto &seq : before.sequences)
                for (const auto &candidate : seq.tracks)
                    if (candidate.id == track)
                        kind = candidate.kind;
        }
        ClipRouting route;
        if (kind == TrackKind::Audio)
            route.video.mode = StreamMode::Disabled;
        else if (!embedded.isChecked())
            route.audio.mode = StreamMode::Disabled;
        const auto clip = *batch
                               .execute(InsertClip{track,
                                                   id,
                                                   time(at.text()),
                                                   {time(source.text()), time(duration.text())},
                                                   route})
                               .clip;
        editor_->commit(std::move(batch));
        sequence_ = sequence;
        selected_ = clip;
        refresh();
        transport_.seek(time(at.text()));
    } catch (const std::exception &e) {
        report(e.what());
    }
}
void Window::playbackSettings() {
    try {
        const auto before = editor_->snapshot();
        const auto *clip = selectedClip(before);
        if (!clip) {
            report("Select a clip to adjust its track and stream routing.");
            return;
        }
        const Track *track = nullptr;
        for (const auto &sequence : before.sequences)
            for (const auto &candidate : sequence.tracks)
                for (const auto &item : candidate.clips)
                    if (item.id == clip->id)
                        track = &candidate;
        if (!track)
            return;
        const auto asset = std::find_if(before.media.begin(), before.media.end(),
                                        [&](const auto &a) { return a.id == clip->media; });
        QDialog dialog(this);
        dialog.setWindowTitle("Track and clip playback");
        dialog.setObjectName("playbackSettingsDialog");
        QFormLayout form(&dialog);
        QCheckBox enabled("Track enabled"), muted("Mute track audio");
        enabled.setObjectName("trackEnabled");
        muted.setObjectName("trackMuted");
        enabled.setChecked(track->playback.enabled);
        muted.setChecked(track->playback.muted);
        QSpinBox gain;
        gain.setObjectName("trackGain");
        gain.setRange(0, 4000);
        gain.setValue(static_cast<int>(track->playback.gain_milli));
        gain.setSuffix(" / 1000");
        QComboBox video, audio;
        video.setObjectName("videoRoute");
        audio.setObjectName("audioRoute");
        for (const auto kind : {TrackKind::Video, TrackKind::Audio}) {
            auto &combo = kind == TrackKind::Video ? video : audio;
            const auto &selected =
                kind == TrackKind::Video ? clip->routing.video : clip->routing.audio;
            combo.addItem("Automatic", -1);
            combo.addItem("Disabled", -2);
            if (asset->source && (kind == TrackKind::Audio || track->kind == TrackKind::Video))
                for (const auto &stream : asset->source->streams)
                    if (stream.kind == kind)
                        combo.addItem("Stream " + QString::number(stream.index) + " (" +
                                          QString::fromStdString(stream.codec) + ")",
                                      QVariant::fromValue<qulonglong>(stream.index));
            combo.setCurrentIndex(
                selected.mode == StreamMode::Automatic ? 0
                : selected.mode == StreamMode::Disabled
                    ? 1
                    : combo.findData(QVariant::fromValue<qulonglong>(selected.index)));
        }
        form.addRow(QString::fromStdString(track->name), &enabled);
        form.addRow(&muted);
        form.addRow("Linear gain", &gain);
        form.addRow("Clip video", &video);
        form.addRow("Clip audio", &audio);
        auto *hint = new QLabel("1000 is unity gain. Mixed audio is clipped at full scale. Track "
                                "order runs from top to bottom.");
        hint->setWordWrap(true);
        form.addRow(hint);
        buttons(dialog, form);
        if (dialog.exec() != QDialog::Accepted)
            return;
        const auto route = [](const QComboBox &combo) {
            const auto value = combo.currentData().toLongLong();
            return StreamSelection{value == -1   ? StreamMode::Automatic
                                   : value == -2 ? StreamMode::Disabled
                                                 : StreamMode::Explicit,
                                   value < 0 ? 0U : static_cast<std::uint32_t>(value)};
        };
        auto batch = editor_->begin(
            {{{"human:desktop"}, ActorKind::Human}, "Track and clip playback", before.revision});
        (void)batch.execute(SetTrackPlayback{
            track->id,
            {enabled.isChecked(), muted.isChecked(), static_cast<std::uint32_t>(gain.value())}});
        (void)batch.execute(SetClipRouting{clip->id, {route(video), route(audio)}});
        editor_->commit(std::move(batch));
        refresh();
    } catch (const std::exception &e) {
        report(e.what());
    }
}
void Window::exportSequence() {
    if (exportWatcher_.isRunning()) {
        report("An export is already running.");
        return;
    }
    if (!sequence_.value) {
        report("Create or select a sequence before exporting.");
        return;
    }
    QString selectedFilter;
    const QString filters = "Lossless reference (Matroska, FFV1 + float PCM) (*.mkv);;"
                            "H.264 delivery (MP4, AAC) (*.mp4)";
    auto destination =
        QFileDialog::getSaveFileName(this, "Export sequence", {}, filters, &selectedFilter);
    if (destination.isEmpty())
        return;
    const auto preset = selectedFilter.startsWith("H.264") ? exporting::Preset::Mp4H264
                                                           : exporting::Preset::LosslessReference;
    const auto wanted = preset == exporting::Preset::Mp4H264 ? ".mp4" : ".mkv";
    if (QFileInfo(destination).suffix().isEmpty())
        destination += wanted;
    try {
        const auto project = editor_->snapshot();
        auto plan = playback::make_sequence_plan(project, sequence_);
        const auto stop = std::make_shared<std::atomic_bool>(false);
        exportStop_ = stop;
        auto *dialog =
            new QProgressDialog("Preparing fixed project revision…", "Cancel", 0, 100, this);
        dialog->setObjectName("exportProgress");
        dialog->setWindowTitle("Export sequence");
        dialog->setWindowModality(Qt::NonModal);
        dialog->setAutoClose(false);
        dialog->setAutoReset(false);
        dialog->setMinimumDuration(0);
        exportDialog_ = dialog;
        connect(dialog, &QProgressDialog::canceled, this, [this, dialog] {
            if (exportStop_)
                exportStop_->store(true);
            dialog->setLabelText("Cancelling export…");
        });
        const bool overwrite = QFileInfo::exists(destination);
        QPointer<QProgressDialog> progress(dialog);
        exporting::Options options{
            media::utf8_path(destination.toStdString()), preset, overwrite,
            [progress](const exporting::Progress &update) {
                if (!progress)
                    return;
                QMetaObject::invokeMethod(
                    progress.data(),
                    [progress, update] {
                        if (!progress)
                            return;
                        progress->setRange(0, static_cast<int>(update.frames_total));
                        progress->setValue(static_cast<int>(update.frames_complete));
                        progress->setLabelText(
                            QString("Revision %1 · frame %2/%3 · audio %4/%5 samples")
                                .arg(update.revision)
                                .arg(update.frames_complete)
                                .arg(update.frames_total)
                                .arg(update.samples_complete)
                                .arg(update.samples_total));
                    },
                    Qt::QueuedConnection);
            }};
        dialog->show();
        exportWatcher_.setFuture(
            QtConcurrent::run([plan = std::move(plan), options, stop]() mutable {
                try {
                    return ExportOutcome{
                        exporting::export_sequence(std::move(plan), options, *stop), {}};
                } catch (const std::exception &error) {
                    return ExportOutcome{{}, QString::fromUtf8(error.what())};
                }
            }));
    } catch (const std::exception &error) {
        exportStop_.reset();
        if (exportDialog_) {
            exportDialog_->deleteLater();
            exportDialog_ = nullptr;
        }
        report(error.what());
    }
}
void Window::sequenceSettings() {
    try {
        const auto before = editor_->snapshot();
        const auto it = std::find_if(before.sequences.begin(), before.sequences.end(),
                                     [this](const auto &s) { return s.id == sequence_; });
        if (it == before.sequences.end()) {
            report("Create a sequence by placing media first.");
            return;
        }
        QDialog dialog(this);
        dialog.setWindowTitle("Sequence output");
        dialog.setObjectName("sequenceSettingsDialog");
        QFormLayout form(&dialog);
        QSpinBox width, height;
        width.setObjectName("sequenceWidth");
        height.setObjectName("sequenceHeight");
        width.setRange(2, 3840);
        height.setRange(2, 2160);
        width.setSingleStep(2);
        height.setSingleStep(2);
        width.setValue(static_cast<int>(it->output.width));
        height.setValue(static_cast<int>(it->output.height));
        QComboBox fps;
        fps.setObjectName("sequenceFps");
        fps.setEditable(true);
        fps.addItems({"24", "25", "30000/1001", "30", "60000/1001", "60"});
        fps.setCurrentText(text({it->frame_duration.rate(), it->frame_duration.value()}));
        form.addRow("Width", &width);
        form.addRow("Height", &height);
        form.addRow("Frames per second", &fps);
        auto *hint = new QLabel("SDR, square pixels, 48 kHz stereo. Use an exact rate such as "
                                "30000/1001. Existing clip positions stay unchanged.");
        hint->setWordWrap(true);
        form.addRow(hint);
        buttons(dialog, form);
        if (dialog.exec() != QDialog::Accepted)
            return;
        const auto rate = time(fps.currentText());
        if (rate < RationalTime{1} || rate > RationalTime{60})
            throw DomainError("Choose an output rate from 1 to 60 fps.");
        editAtRevision(SetSequenceOutput{sequence_,
                                         {rate.rate(), rate.value()},
                                         {static_cast<std::uint32_t>(width.value()),
                                          static_cast<std::uint32_t>(height.value()), 48000, 2}},
                       before.revision, "Sequence output settings");
    } catch (const std::exception &e) {
        report(e.what());
    }
}
} // namespace nle::desktop
