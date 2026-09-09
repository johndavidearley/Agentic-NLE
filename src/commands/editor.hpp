#pragma once
#include "project/model.hpp"
#include <optional>
#include <variant>

namespace nle {
struct CreateSequence {
    std::string name;
    RationalTime frame_duration{1, 24};
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
};
struct InsertClip {
    TrackId track;
    MediaId media;
    RationalTime position;
    TimeRange source;
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
using Command = std::variant<CreateSequence, CreateTrack, RegisterMedia, InsertClip, MoveClip,
                             TrimClip, SplitClip, DeleteClip>;
struct CommandResult {
    std::optional<SequenceId> sequence;
    std::optional<TrackId> track;
    std::optional<MediaId> media;
    std::optional<ClipId> clip; // For split, the newly created right-hand clip.
};
class Editor {
  public:
    explicit Editor(std::string name);
    explicit Editor(ProjectSnapshot project);
    [[nodiscard]] ProjectSnapshot snapshot() const { return state_; }
    CommandResult execute(const Command &command);
    bool undo();
    bool redo();
    [[nodiscard]] bool can_undo() const { return !undo_.empty(); }
    [[nodiscard]] bool can_redo() const { return !redo_.empty(); }

  private:
    struct Edit {
        ProjectSnapshot before;
        ProjectSnapshot after;
    };
    ProjectSnapshot state_;
    std::vector<Edit> undo_;
    std::vector<Edit> redo_;
};
} // namespace nle
