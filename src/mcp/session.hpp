#pragma once
#include "export/export.hpp"
#include "mcp/media_access.hpp"
#include "mcp/output_access.hpp"
#include "mcp/wire.hpp"
#include <chrono>
#include <functional>
#include <future>
namespace nle::mcp {
using Clock = std::chrono::steady_clock;
struct Policy {
    bool allow_edit = false, allow_save = false;
    std::optional<MediaAccess> media_access;
    std::optional<OutputAccess> output_access;
    std::function<exporting::Result(ProjectSnapshot, SequenceId, exporting::Options,
                                    std::atomic_bool &)>
        export_sequence;
    std::optional<std::uint64_t> saved_revision;
    Actor actor{ActorId{"agent:mcp"}, ActorKind::Agent};
    std::chrono::seconds session_lifetime{3600}, proposal_lifetime{120};
    std::size_t max_proposals = 4, max_requests = 256, max_memo_bytes = 32 * 1024 * 1024;
};
class Session {
  public:
    Session(ProjectSnapshot project, Policy policy = {},
            std::function<void(const ProjectSnapshot &)> save = {},
            std::function<Clock::time_point()> now = Clock::now,
            std::function<void(const ProjectSnapshot &)> checkpoint = {});
    ~Session();
    std::optional<Json> receive(std::string_view message);
    std::optional<Json> dispatch(const Json &message);
    ProjectSnapshot current() const { return editor_.snapshot(); }

  private:
    struct Proposal {
        Transaction transaction;
        Clock::time_point expires;
        std::size_t bytes;
    };
    struct Memo {
        std::string fingerprint;
        Json result;
    };
    struct ProbedMedia {
        media::ProbeResult probe;
        std::filesystem::file_time_type modified;
    };
    struct ProbeJob {
        enum class State { Running, Ready, Failed, Cancelled, Stale, Committed };
        State state = State::Running;
        std::filesystem::path path;
        std::optional<MediaId> relink_media;
        std::uint64_t base_revision = 0;
        Clock::time_point expires;
        std::shared_ptr<std::atomic_bool> stop;
        std::future<ProbedMedia> future;
        std::optional<ProbedMedia> result;
        std::string error;
    };
    struct ExportProgress {
        std::mutex mutex;
        exporting::Progress value;
    };
    struct ExportJob {
        std::uint64_t revision = 0;
        std::filesystem::path destination;
        std::shared_ptr<std::atomic_bool> stop;
        std::shared_ptr<ExportProgress> progress;
        std::future<exporting::Result> future;
        std::optional<exporting::Result> result;
        std::string status = "running", error;
    };
    Editor editor_;
    Policy policy_;
    std::function<void(const ProjectSnapshot &)> save_, checkpoint_;
    std::optional<std::uint64_t> recovery_revision_;
    std::string recovery_error_;
    std::function<Clock::time_point()> now_;
    Clock::time_point expires_;
    std::string session_id_, project_id_;
    std::uint64_t next_proposal_ = 0, next_probe_ = 0, saved_revision_ = 0;
    bool initialized_ = false, ready_ = false;
    std::map<std::string, Proposal> proposals_;
    std::map<std::string, ProbeJob> probes_;
    std::map<std::string, ExportJob> exports_;
    std::map<std::string, Memo> memos_;
    std::size_t proposal_bytes_ = 0, memo_bytes_ = 0;
    Json call(const std::string &name, const Json &arguments);
    Json run(const std::string &name, const Json &arguments);
    Json info() const;
    Json outcome() const;
    RequestContext context(const Json &arguments, std::string default_label) const;
    void expire_proposals();
    void refresh_probe(ProbeJob &job);
    void expire_probes();
    ProbeJob &probe_job(const Json &arguments);
    void refresh_export(ExportJob &job);
    Json export_status(const std::string &id, ExportJob &job);
    void require_project(const Json &arguments) const;
};
} // namespace nle::mcp
