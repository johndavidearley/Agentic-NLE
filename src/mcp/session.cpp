#include "mcp/session.hpp"
#include "project/document.hpp"
#include <algorithm>
#include <iomanip>
#include <random>
#include <sstream>
namespace nle::mcp {
namespace {
Json rpc_error(const Json &id, int code, const std::string &message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}
Json failure(const std::string &code, const std::string &message) {
    return {{"ok", false}, {"error", {{"code", code}, {"message", message}}}};
}
void payload_limit(const Json &value) {
    if (value.dump().size() > max_payload_bytes)
        throw Failure("response_limit", "Result exceeds the 2 MiB session profile.");
}
bool letter(char value) { return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z'); }
} // namespace
Session::Session(ProjectSnapshot project, Policy policy,
                 std::function<void(const ProjectSnapshot &)> save,
                 std::function<Clock::time_point()> now,
                 std::function<void(const ProjectSnapshot &)> checkpoint)
    : editor_(std::move(project)), policy_(std::move(policy)), save_(std::move(save)),
      checkpoint_(std::move(checkpoint)), now_(std::move(now)),
      expires_(now_() + policy_.session_lifetime) {
    validate_actor(policy_.actor);
    if (policy_.actor.kind != ActorKind::Agent ||
        policy_.session_lifetime <= std::chrono::seconds::zero() ||
        policy_.proposal_lifetime <= std::chrono::seconds::zero())
        throw Failure("configuration", "Invalid session policy.");
    const auto state = editor_.snapshot();
    if (snapshot_bytes(state) > 8 * 1024 * 1024)
        throw Failure("project_limit", "Project exceeds the 8 MiB accounted session profile.");
    project_id_ = std::to_string(state.id.value);
    saved_revision_ = policy_.saved_revision.value_or(state.revision);
    std::random_device random;
    std::ostringstream nonce;
    nonce << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i)
        nonce << std::setw(8) << random();
    session_id_ = nonce.str();
}
Json Session::outcome() const {
    return {{"ok", true},
            {"project_id", project_id_},
            {"revision", std::to_string(editor_.revision())}};
}
Json Session::info() const {
    auto result = outcome();
    result["session_id"] = session_id_;
    result["actor_id"] = policy_.actor.id.value;
    result["allow_edit"] = policy_.allow_edit;
    result["allow_save"] = policy_.allow_save;
    result["recovery_enabled"] = static_cast<bool>(checkpoint_);
    result["recovery_revision"] =
        recovery_revision_ ? Json(std::to_string(*recovery_revision_)) : Json(nullptr);
    result["recovery_error"] = recovery_error_;
    result["dirty"] = editor_.revision() != saved_revision_;
    result["expires_in_seconds"] = std::max<std::int64_t>(
        0, std::chrono::duration_cast<std::chrono::seconds>(expires_ - now_()).count());
    const auto state = editor_.snapshot();
    result["name"] = state.name;
    result["media_count"] = state.media.size();
    result["sequence_count"] = state.sequences.size();
    const auto history = editor_.history_usage();
    result["history"] = {{"undo_entries", history.undo_entries},
                         {"redo_entries", history.redo_entries},
                         {"accounted_bytes", history.bytes}};
    result["active_proposals"] = proposals_.size();
    result["recorded_requests"] = memos_.size();
    return result;
}
void Session::require_project(const Json &arguments) const {
    if (std::to_string(number(arguments.at("project_id"), false)) != project_id_)
        throw Failure("wrong_project", "This request belongs to a different project.");
}
RequestContext Session::context(const Json &arguments, std::string default_label) const {
    return {policy_.actor,
            arguments.contains("label") ? text(arguments.at("label")) : std::move(default_label),
            number(arguments.at("expected_revision"))};
}
void Session::expire_proposals() {
    for (auto it = proposals_.begin(); it != proposals_.end();) {
        if (it->second.expires <= now_()) {
            proposal_bytes_ -= it->second.bytes;
            it = proposals_.erase(it);
        } else
            ++it;
    }
}
Json Session::run(const std::string &name, const Json &a) {
    if (name == "project_get") {
        fields(a, {});
        return info();
    }
    if (name == "project_snapshot") {
        fields(a, {"project_id"});
        require_project(a);
        auto result = outcome();
        result["snapshot"] = snapshot(editor_.snapshot());
        payload_limit(result);
        return result;
    }
    if (name == "project_changes") {
        fields(a, {"project_id", "since_revision"}, {"limit"});
        require_project(a);
        std::size_t limit = 100;
        if (a.contains("limit")) {
            if (!a.at("limit").is_number_integer() || a.at("limit") < 1 || a.at("limit") > 100)
                throw Failure("invalid_arguments",
                              "Change page limit must be an integer from 1 to 100.");
            limit = a.at("limit").get<std::size_t>();
        }
        const auto since = number(a.at("since_revision"));
        const auto changes = editor_.changes_since(since);
        auto result = outcome();
        result["operations"] = Json::array();
        std::uint64_t cursor = since;
        for (std::size_t i = 0; i < std::min(limit, changes.size()); ++i) {
            result["operations"].push_back(operation(changes[i]));
            cursor = changes[i].revision;
        }
        result["next_revision"] = std::to_string(cursor);
        result["has_more"] = changes.size() > limit;
        payload_limit(result);
        return result;
    }
    if (name == "edit_preview") {
        fields(a, {"session_id", "project_id", "expected_revision", "request_key", "label",
                   "commands"});
        if (!a.at("commands").is_array() || a.at("commands").empty() ||
            a.at("commands").size() > 128)
            throw Failure("invalid_arguments", "A batch requires 1 to 128 commands.");
        if (proposals_.size() >= policy_.max_proposals)
            throw Failure("proposal_limit",
                          "Discard or commit a proposal before preparing another.");
        auto batch = editor_.begin(context(a, "Agent edit"));
        Aliases aliases;
        Json results = Json::array();
        for (const auto &edit : a.at("commands")) {
            const auto result = batch.execute(command(edit, aliases));
            if (edit.contains("as")) {
                const auto alias = text(edit.at("as"), 64);
                if (!letter(alias.front()) ||
                    !std::all_of(
                        alias.begin(), alias.end(),
                        [](char c) { return letter(c) || (c >= '0' && c <= '9') || c == '_'; }) ||
                    !aliases.emplace(alias, result).second)
                    throw Failure("invalid_arguments", "Invalid or duplicate batch alias.");
            }
            results.push_back(command_result(result));
        }
        const auto candidate = batch.preview();
        const auto bytes = snapshot_bytes(candidate);
        if (bytes > 8 * 1024 * 1024 || proposal_bytes_ + bytes > 32 * 1024 * 1024)
            throw Failure("proposal_limit", "Proposed project exceeds the session memory profile.");
        const auto proposal_id = session_id_ + ":" + std::to_string(++next_proposal_);
        auto result = outcome();
        result["proposal_id"] = proposal_id;
        result["provisional"] = true;
        result["base_revision"] = std::to_string(candidate.revision);
        result["expires_in_seconds"] = policy_.proposal_lifetime.count();
        result["results"] = results;
        result["snapshot"] = snapshot(candidate);
        payload_limit(result);
        proposals_.emplace(proposal_id,
                           Proposal{std::move(batch), now_() + policy_.proposal_lifetime, bytes});
        proposal_bytes_ += bytes;
        return result;
    }
    if (name == "edit_commit" || name == "edit_rollback") {
        fields(a, {"session_id", "project_id", "expected_revision", "request_key", "proposal_id"});
        if (name == "edit_commit" && !policy_.allow_edit)
            throw Failure("permission_denied", "This session has no edit permission.");
        const auto id = text(a.at("proposal_id"), 128);
        const auto found = proposals_.find(id);
        if (found == proposals_.end())
            throw Failure("proposal_missing",
                          "Proposal is expired, closed or belongs to another session.");
        auto proposal = proposals_.extract(found);
        proposal_bytes_ -= proposal.mapped().bytes;
        if (number(a.at("expected_revision")) != editor_.revision())
            throw RevisionConflict("Expected revision is stale.");
        if (name == "edit_rollback") {
            proposal.mapped().transaction.rollback();
            return outcome();
        }
        const auto committed = editor_.commit(std::move(proposal.mapped().transaction));
        auto result = outcome();
        result["operation_id"] = committed ? Json(std::to_string(committed->value)) : Json(nullptr);
        return result;
    }
    if (name == "history_undo" || name == "history_redo") {
        fields(a, {"session_id", "project_id", "expected_revision", "request_key"}, {"label"});
        if (!policy_.allow_edit)
            throw Failure("permission_denied", "This session has no edit permission.");
        const bool changed = name == "history_undo" ? editor_.undo(context(a, "Agent undo"))
                                                    : editor_.redo(context(a, "Agent redo"));
        auto result = outcome();
        result["changed"] = changed;
        return result;
    }
    if (name == "project_save") {
        fields(a, {"session_id", "project_id", "expected_revision", "request_key"});
        if (!policy_.allow_save || !save_)
            throw Failure("permission_denied", "This session has no save permission.");
        if (number(a.at("expected_revision")) != editor_.revision())
            throw RevisionConflict("Expected revision is stale.");
        save_(editor_.snapshot());
        saved_revision_ = editor_.revision();
        recovery_error_.clear();
        recovery_revision_.reset();
        auto result = outcome();
        result["saved_revision"] = std::to_string(saved_revision_);
        return result;
    }
    throw Failure("invalid_arguments", "Unknown tool.");
}
Json Session::call(const std::string &name, const Json &arguments) {
    const auto safely = [&]() -> Json {
        try {
            auto result = run(name, arguments);
            const bool changed = (name == "edit_commit" && !result.at("operation_id").is_null()) ||
                                 ((name == "history_undo" || name == "history_redo") &&
                                  result.at("changed").get<bool>());
            if (checkpoint_ && changed) {
                try {
                    checkpoint_(editor_.snapshot());
                    recovery_revision_ = editor_.revision();
                    recovery_error_.clear();
                } catch (const std::exception &) {
                    recovery_error_ = "Edit succeeded, but recovery checkpoint failed. Save "
                                      "explicitly before closing.";
                }
                result["recovery_error"] = recovery_error_;
            }
            return result;
        } catch (const RevisionConflict &) {
            return failure("revision_conflict", "Expected revision is stale; inspect the current "
                                                "project and prepare a new request.");
        } catch (const FileError &e) {
            return failure(e.code, e.what());
        } catch (const Failure &e) {
            return failure(e.code, e.what());
        } catch (const DomainError &e) {
            return failure("invalid_edit", e.what());
        } catch (const Json::exception &) {
            return failure("invalid_arguments", "Invalid JSON value or missing field.");
        } catch (const std::exception &) {
            return failure("internal_error", "The request could not be completed.");
        }
    };
    expire_proposals();
    if (now_() >= expires_)
        return failure("session_expired", "Session expired. Start a new server session.");
    if (name == "project_get" || name == "project_snapshot" || name == "project_changes")
        return safely();
    try {
        if (!arguments.is_object() || !arguments.contains("session_id") ||
            !arguments.contains("project_id") || !arguments.contains("request_key") ||
            !arguments.contains("expected_revision"))
            throw Failure("invalid_arguments",
                          "Session, project, expected revision and request key are required.");
        if (text(arguments.at("session_id"), 128) != session_id_)
            throw Failure("wrong_session", "This request belongs to a different session.");
        require_project(arguments);
        (void)number(arguments.at("expected_revision"));
        const auto key = text(arguments.at("request_key"), 128);
        const auto fingerprint = Json{{"tool", name}, {"arguments", arguments}}.dump();
        if (const auto previous = memos_.find(key); previous != memos_.end()) {
            if (previous->second.fingerprint != fingerprint)
                throw Failure("idempotency_conflict",
                              "Request key was already used with different arguments.");
            return previous->second.result;
        }
        const bool checkpoint = name == "project_save";
        if (checkpoint)
            fields(arguments, {"session_id", "project_id", "expected_revision", "request_key"});
        const auto entry_limit = policy_.max_requests + (checkpoint ? 1U : 0U);
        const auto reserve = checkpoint ? std::size_t{4096} : max_payload_bytes + 8192;
        if (memos_.size() >= entry_limit ||
            memo_bytes_ + fingerprint.size() + reserve > policy_.max_memo_bytes)
            throw Failure("session_limit", "Retry record budget is full. Use the reserved save "
                                           "request, then start a new session.");
        // Reserve the entry before any live mutation. Entries, including failures, never evict.
        auto [entry, inserted] = memos_.emplace(key, Memo{fingerprint, nullptr});
        (void)inserted;
        entry->second.result = safely();
        memo_bytes_ += fingerprint.size() + entry->second.result.dump().size();
        return entry->second.result;
    } catch (const Failure &e) {
        return failure(e.code, e.what());
    } catch (const DomainError &) {
        return failure("invalid_arguments", "Invalid request context.");
    } catch (const Json::exception &) {
        return failure("invalid_arguments", "Invalid request context.");
    }
}
std::optional<Json> Session::receive(std::string_view message) {
    try {
        return dispatch(parse(message));
    } catch (const std::exception &) {
        return rpc_error(nullptr, -32700, "Invalid JSON message.");
    }
}
std::optional<Json> Session::dispatch(const Json &message) {
    Json id = nullptr;
    if (!message.is_object() || !message.contains("jsonrpc") || message.at("jsonrpc") != "2.0" ||
        !message.contains("method") || !message.at("method").is_string())
        return rpc_error(id, -32600, "Invalid request.");
    if (message.contains("id")) {
        id = message.at("id");
        if ((!id.is_string() && !id.is_number_integer()) ||
            (id.is_string() && (id.get_ref<const std::string &>().empty() ||
                                id.get_ref<const std::string &>().size() > 128)))
            return rpc_error(nullptr, -32600, "Invalid request ID.");
    }
    const auto method = message.at("method").get<std::string>();
    if (!message.contains("id")) {
        if (method == "notifications/initialized" && initialized_)
            ready_ = true;
        return {};
    }
    const auto response = [&](Json result) {
        return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
    };
    try {
        const auto params = message.value("params", Json::object());
        if (!params.is_object())
            return rpc_error(id, -32602, "Parameters must be an object.");
        if (method == "ping")
            return response(Json::object());
        if (method == "initialize") {
            if (initialized_)
                return rpc_error(id, -32600, "Session is already initialized.");
            if (!params.contains("protocolVersion") || !params.at("protocolVersion").is_string() ||
                !params.contains("capabilities") || !params.at("capabilities").is_object() ||
                !params.contains("clientInfo") || !params.at("clientInfo").is_object())
                return rpc_error(id, -32602, "Invalid initialization parameters.");
            (void)text(params.at("protocolVersion"), 128);
            if (!params.at("clientInfo").contains("name") ||
                !params.at("clientInfo").contains("version"))
                return rpc_error(id, -32602, "Client name and version are required.");
            (void)text(params.at("clientInfo").at("name"));
            (void)text(params.at("clientInfo").at("version"));
            initialized_ = true;
            return response(
                {{"protocolVersion", "2025-11-25"},
                 {"serverInfo", {{"name", "agentic-nle"}, {"version", "0.7.0"}}},
                 {"capabilities", {{"tools", {{"listChanged", false}}}}},
                 {"instructions",
                  "Start with project_get. Edits require edit_preview then edit_commit; saving is "
                  "separate. Use the current project/session/revision and a new request_key for "
                  "each intent. Retry identical requests with their original key. One writer per "
                  "project. Session lifetime is one hour; save before expiry or retry-budget "
                  "exhaustion. Media import is performed in the desktop or CLI before starting "
                  "this session."}});
        }
        if (method != "tools/list" && method != "tools/call")
            return rpc_error(id, -32601, "Method not found.");
        if (!ready_)
            return rpc_error(id, -32002, "Initialize the session first.");
        if (method == "tools/list") {
            fields(params, {}, {"cursor", "_meta"});
            if (params.contains("cursor"))
                return rpc_error(id, -32602, "No further tool pages exist.");
            return response({{"tools", tool_catalog()}});
        }
        if (method == "tools/call") {
            fields(params, {"name"}, {"arguments", "_meta"});
            const auto name = text(params.at("name"), 128);
            const auto catalog = tool_catalog();
            if (std::none_of(catalog.begin(), catalog.end(),
                             [&](const auto &tool) { return tool.at("name") == name; }))
                return rpc_error(id, -32602, "Unknown tool.");
            const auto arguments = params.value("arguments", Json::object());
            if (!arguments.is_object())
                return rpc_error(id, -32602, "Tool arguments must be an object.");
            const auto result = call(name, arguments);
            return response(
                {{"content", Json::array({{{"type", "text"}, {"text", result.dump()}}})},
                 {"structuredContent", result},
                 {"isError", !result.at("ok").get<bool>()}});
        }
        return rpc_error(id, -32601, "Method not found.");
    } catch (const Failure &) {
        return rpc_error(id, -32602, "Invalid parameters.");
    } catch (const DomainError &) {
        return rpc_error(id, -32602, "Invalid parameters.");
    } catch (const Json::exception &) {
        return rpc_error(id, -32602, "Invalid parameters.");
    } catch (const std::exception &) {
        return rpc_error(id, -32603, "Internal error.");
    }
}
} // namespace nle::mcp
