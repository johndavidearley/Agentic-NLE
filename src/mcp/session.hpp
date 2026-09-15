#pragma once
#include "mcp/wire.hpp"
#include <chrono>
#include <functional>
namespace nle::mcp {
using Clock = std::chrono::steady_clock;
struct Policy {
    bool allow_edit = false, allow_save = false;
    Actor actor{ActorId{"agent:mcp"}, ActorKind::Agent};
    std::chrono::seconds session_lifetime{3600}, proposal_lifetime{120};
    std::size_t max_proposals = 4, max_requests = 256, max_memo_bytes = 32 * 1024 * 1024;
};
class Session {
  public:
    Session(ProjectSnapshot project, Policy policy = {},
            std::function<void(const ProjectSnapshot &)> save = {},
            std::function<Clock::time_point()> now = Clock::now);
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
    Editor editor_;
    Policy policy_;
    std::function<void(const ProjectSnapshot &)> save_;
    std::function<Clock::time_point()> now_;
    Clock::time_point expires_;
    std::string session_id_, project_id_;
    std::uint64_t next_proposal_ = 0, saved_revision_ = 0;
    bool initialized_ = false, ready_ = false;
    std::map<std::string, Proposal> proposals_;
    std::map<std::string, Memo> memos_;
    std::size_t proposal_bytes_ = 0, memo_bytes_ = 0;
    Json call(const std::string &name, const Json &arguments);
    Json run(const std::string &name, const Json &arguments);
    Json info() const;
    Json outcome() const;
    RequestContext context(const Json &arguments, std::string default_label) const;
    void expire_proposals();
    void require_project(const Json &arguments) const;
};
} // namespace nle::mcp
