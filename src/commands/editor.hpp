#pragma once
#include "project/model.hpp"
#include <memory>
#include <mutex>
#include <optional>
#include <variant>

namespace nle {
struct CreateSequence {
    std::string name;
    RationalTime frame_duration{1, 24};
};
struct SetSequenceOutput {
    SequenceId sequence;
    RationalTime frame_duration;
    SequenceOutput output;
};
struct SetTrackPlayback {
    TrackId track;
    TrackPlayback playback;
};
struct SetClipRouting {
    ClipId clip;
    ClipRouting routing;
};
struct CreateTrack {
    SequenceId sequence;
    TrackKind kind;
    std::string name;
};
struct RegisterMedia {
    std::string name;
    MediaKind kind;
    RationalTime duration;
    std::vector<MediaLocation> locations;
    std::optional<SourceMetadata> source{};
};
struct InsertClip {
    TrackId track;
    MediaId media;
    RationalTime position;
    TimeRange source;
    ClipRouting routing{};
};
struct MoveClip {
    ClipId clip;
    TrackId track;
    RationalTime position;
};
struct TrimClip {
    ClipId clip;
    RationalTime position;
    TimeRange source;
};
struct SplitClip {
    ClipId clip;
    RationalTime position; // Absolute timeline position, strictly inside the clip.
};
struct DeleteClip {
    ClipId clip;
};
struct DeleteTrack {
    TrackId track;
}; // Cascades to all clips in this track.
struct ReorderTrack {
    SequenceId sequence;
    TrackId track;
    std::size_t index;
};
struct RelinkMedia {
    MediaId media;
    LocationRole role;
    std::optional<std::string> uri;
};
struct ReplaceMediaSource {
    MediaId media;
    std::string uri;
    SourceMetadata source;
};
using Command =
    std::variant<CreateSequence, CreateTrack, RegisterMedia, InsertClip, MoveClip, TrimClip,
                 SplitClip, DeleteClip, DeleteTrack, ReorderTrack, RelinkMedia, ReplaceMediaSource,
                 SetSequenceOutput, SetTrackPlayback, SetClipRouting>;
struct CommandResult {
    std::optional<OperationId> operation;
    std::uint64_t revision = 0;
    std::optional<SequenceId> sequence;
    std::optional<TrackId> track;
    std::optional<MediaId> media;
    std::optional<ClipId> clip; // For split, the newly created right-hand clip.
};
struct RequestContext {
    Actor actor{};
    std::string label = "Edit";
    std::optional<std::uint64_t> expected_revision{};
};
struct HistoryLimits {
    std::size_t entries = 100;
    std::size_t bytes = 64 * 1024 * 1024;
};
struct HistoryUsage {
    std::size_t undo_entries;
    std::size_t redo_entries;
    std::size_t bytes; // Conservative retained payload accounting, not process RSS.
};
class RevisionConflict : public DomainError {
  public:
    using DomainError::DomainError;
};
class Transaction {
  public:
    Transaction(Transaction &&) noexcept = default;
    Transaction &operator=(Transaction &&) noexcept = default;
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;
    CommandResult execute(const Command &command);
    [[nodiscard]] ProjectSnapshot preview() const;
    [[nodiscard]] bool active() const { return candidate_.has_value() && !owner_.expired(); }
    void rollback() noexcept {
        candidate_.reset();
        actions_.clear();
        owner_.reset();
    }

  private:
    friend class Editor;
    Transaction(ProjectSnapshot state, RequestContext context, std::weak_ptr<const int> owner);
    std::optional<ProjectSnapshot> candidate_;
    RequestContext context_;
    std::weak_ptr<const int> owner_;
    std::uint64_t base_revision_;
    std::vector<std::string> actions_;
    std::size_t commands_ = 0;
};
// Editor entry points are synchronized. A Transaction is single-owner and not thread-safe.
class Editor {
  public:
    explicit Editor(std::string name, HistoryLimits limits = {});
    explicit Editor(ProjectSnapshot project, HistoryLimits limits = {});
    [[nodiscard]] ProjectSnapshot snapshot() const;
    [[nodiscard]] std::uint64_t revision() const;
    CommandResult execute(const Command &command, RequestContext context = {});
    Transaction begin(RequestContext context = {}) const;
    std::optional<OperationId> commit(Transaction &&transaction);
    bool undo(RequestContext context = {});
    bool redo(RequestContext context = {});
    [[nodiscard]] bool can_undo() const;
    [[nodiscard]] bool can_redo() const;
    [[nodiscard]] HistoryUsage history_usage() const;
    // Pull-based edit notifications; records survive save/load and undo/redo.
    [[nodiscard]] std::vector<OperationRecord> changes_since(std::uint64_t revision) const;

  private:
    struct Edit {
        ProjectSnapshot before;
        ProjectSnapshot after;
        OperationId operation;
        std::size_t bytes;
    };
    void check_context(const RequestContext &context) const;
    std::optional<OperationId> publish(ProjectSnapshot candidate, const RequestContext &context,
                                       std::vector<std::string> actions);
    bool restore(bool redo, const RequestContext &context);
    ProjectSnapshot state_;
    HistoryLimits limits_;
    std::vector<std::shared_ptr<const Edit>> undo_;
    std::vector<std::shared_ptr<const Edit>> redo_;
    std::shared_ptr<const int> identity_ = std::make_shared<const int>(0);
    mutable std::mutex mutex_;
};
} // namespace nle