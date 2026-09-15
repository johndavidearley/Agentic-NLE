#include "mcp/session.hpp"
#include <iostream>
using namespace nle;
using namespace nle::mcp;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error("check failed: " #x);                                         \
    } while (false)
ProjectSnapshot fixture() {
    Editor editor("Agent project");
    editor.execute(RegisterMedia{"Existing asset", MediaKind::AudioVideo, {60}, {}});
    auto result = editor.snapshot();
    result.id = ProjectId{18446744073709551599ULL};
    return result;
}
void initialize(Session &session) {
    const auto result =
        session.dispatch({{"jsonrpc", "2.0"},
                          {"id", 1},
                          {"method", "initialize"},
                          {"params",
                           {{"protocolVersion", "2025-11-25"},
                            {"capabilities", Json::object()},
                            {"clientInfo", {{"name", "test"}, {"version", "1"}}}}}});
    CHECK(result->at("result").at("protocolVersion") == "2025-11-25");
    CHECK(!session.dispatch({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}));
}
Json call(Session &session, const std::string &name, const Json &args = Json::object()) {
    const auto reply = session.dispatch({{"jsonrpc", "2.0"},
                                         {"id", "call"},
                                         {"method", "tools/call"},
                                         {"params", {{"name", name}, {"arguments", args}}}});
    CHECK(reply && reply->contains("result"));
    const auto &result = reply->at("result");
    CHECK(Json::parse(result.at("content")[0].at("text").get<std::string>()) ==
          result.at("structuredContent"));
    CHECK(result.at("isError") == !result.at("structuredContent").at("ok").get<bool>());
    return result.at("structuredContent");
}
Json request(Session &session, const std::string &key) {
    const auto info = call(session, "project_get");
    return {{"project_id", info.at("project_id")},
            {"session_id", info.at("session_id")},
            {"expected_revision", info.at("revision")},
            {"request_key", key}};
}
Json batch(Session &session, const std::string &key) {
    auto args = request(session, key);
    args["label"] = "Build an opening";
    args["commands"] = Json::array({{{"op", "create_sequence"},
                                     {"name", "Main"},
                                     {"as", "sequence"},
                                     {"frame_duration", time(RationalTime{1001, 30000})}},
                                    {{"op", "create_track"},
                                     {"sequence_id", "$sequence"},
                                     {"kind", "video"},
                                     {"name", "V1"},
                                     {"as", "video"}},
                                    {{"op", "insert_clip"},
                                     {"track_id", "$video"},
                                     {"media_id", "1"},
                                     {"position", time(RationalTime{})},
                                     {"source_in", time(RationalTime{5})},
                                     {"duration", time(RationalTime{10})},
                                     {"as", "opening"}},
                                    {{"op", "trim_clip"},
                                     {"clip_id", "$opening"},
                                     {"position", time(RationalTime{1})},
                                     {"source_in", time(RationalTime{6})},
                                     {"duration", time(RationalTime{8})}},
                                    {{"op", "split_clip"},
                                     {"clip_id", "$opening"},
                                     {"position", time(RationalTime{5})},
                                     {"as", "right"}},
                                    {{"op", "delete_clip"}, {"clip_id", "$right"}}});
    return args;
}
void rejected(const Json &result, const std::string &code) {
    CHECK(result.at("ok") == false);
    CHECK(result.at("error").at("code") == code);
}
void equivalence() {
    const auto base = fixture();
    Policy policy;
    policy.allow_edit = policy.allow_save = true;
    policy.actor.id.value = "agent:tests";
    ProjectSnapshot saved = base;
    int saves = 0;
    Session session(base, policy, [&](const auto &state) {
        saved = state;
        ++saves;
    });
    initialize(session);
    CHECK(call(session, "project_get").at("project_id") == "18446744073709551599");
    auto args = batch(session, "preview");
    const auto preview = call(session, "edit_preview", args);
    CHECK(preview.at("ok") == true && preview.at("provisional") == true);
    CHECK(session.current() == base);
    CHECK(call(session, "edit_preview", args) == preview);
    CHECK(call(session, "project_get").at("active_proposals") == 1);
    auto commit = request(session, "commit");
    commit["proposal_id"] = preview.at("proposal_id");
    const auto committed = call(session, "edit_commit", commit);
    CHECK(committed.at("ok") == true);
    Editor local(base);
    auto transaction = local.begin({policy.actor, "Build an opening", base.revision});
    const auto sequence = *transaction.execute(CreateSequence{"Main", {1001, 30000}}).sequence;
    const auto track = *transaction.execute(CreateTrack{sequence, TrackKind::Video, "V1"}).track;
    const auto clip = *transaction.execute(InsertClip{track, MediaId{1}, {}, {{5}, {10}}}).clip;
    transaction.execute(TrimClip{clip, {1}, {{6}, {8}}});
    const auto right = *transaction.execute(SplitClip{clip, {5}}).clip;
    transaction.execute(DeleteClip{right});
    local.commit(std::move(transaction));
    CHECK(session.current() == local.snapshot());
    CHECK(call(session, "edit_commit", commit) == committed);
    CHECK(session.current() == local.snapshot());
    auto changed_key = commit;
    changed_key["proposal_id"] = "another";
    rejected(call(session, "edit_commit", changed_key), "idempotency_conflict");
    auto stale = request(session, "stale");
    stale["expected_revision"] = "0";
    rejected(call(session, "history_undo", stale), "revision_conflict");
    CHECK(session.current() == local.snapshot());
    auto undo = request(session, "undo");
    CHECK(call(session, "history_undo", undo).at("changed") == true);
    local.undo({policy.actor, "Agent undo", local.revision()});
    CHECK(session.current() == local.snapshot());
    auto redo = request(session, "redo");
    CHECK(call(session, "history_redo", redo).at("changed") == true);
    local.redo({policy.actor, "Agent redo", local.revision()});
    CHECK(session.current() == local.snapshot());
    // The original successful commit remains replayable even after subsequent revisions.
    CHECK(call(session, "edit_commit", commit) == committed);
    CHECK(session.current() == local.snapshot());
    auto save = request(session, "save");
    CHECK(call(session, "project_save", save).at("ok") == true);
    CHECK(call(session, "project_save", save).at("ok") == true && saves == 1 &&
          saved == session.current());
    CHECK(call(session, "project_get").at("dirty") == false);
    const auto read =
        call(session, "project_snapshot", {{"project_id", std::to_string(base.id.value)}});
    CHECK(read.at("snapshot") == snapshot(session.current()));
    auto page = call(
        session, "project_changes",
        {{"project_id", std::to_string(base.id.value)}, {"since_revision", "0"}, {"limit", 2}});
    CHECK(page.at("operations").size() == 2 && page.at("has_more") == true);
    page = call(session, "project_changes",
                {{"project_id", std::to_string(base.id.value)},
                 {"since_revision", page.at("next_revision")}});
    CHECK(page.at("operations").size() == 2 && page.at("has_more") == false);
    args = batch(session, "failed-preview");
    args["commands"].push_back({{"op", "delete_clip"}, {"clip_id", "99999"}});
    rejected(call(session, "edit_preview", args), "invalid_edit");
    CHECK(session.current() == local.snapshot());
    args = batch(session, "wrong-alias");
    args["commands"][2]["track_id"] = "$sequence";
    rejected(call(session, "edit_preview", args), "invalid_arguments");
    args = batch(session, "spoofed");
    args["actor_id"] = "human:owner";
    rejected(call(session, "edit_preview", args), "invalid_arguments");
}
void stale_and_permissions() {
    const auto base = fixture();
    Session readonly(base);
    initialize(readonly);
    const auto preview = call(readonly, "edit_preview", batch(readonly, "preview"));
    CHECK(preview.at("ok") == true);
    auto commit = request(readonly, "commit");
    commit["proposal_id"] = preview.at("proposal_id");
    rejected(call(readonly, "edit_commit", commit), "permission_denied");
    rejected(call(readonly, "history_undo", request(readonly, "undo")), "permission_denied");
    rejected(call(readonly, "project_save", request(readonly, "save")), "permission_denied");
    CHECK(readonly.current() == base);
    auto args = batch(readonly, "wrong-project");
    args["project_id"] = "10";
    rejected(call(readonly, "edit_preview", args), "wrong_project");
    args = batch(readonly, "wrong-session");
    args["session_id"] = "old-session";
    rejected(call(readonly, "edit_preview", args), "wrong_session");
    args = batch(readonly, "numeric-id");
    args["project_id"] = base.id.value;
    rejected(call(readonly, "edit_preview", args), "invalid_arguments");
    Policy policy;
    policy.allow_edit = true;
    Session writable(base, policy);
    initialize(writable);
    const auto first = call(writable, "edit_preview", batch(writable, "first"));
    const auto second = call(writable, "edit_preview", batch(writable, "second"));
    commit = request(writable, "first-commit");
    commit["proposal_id"] = first.at("proposal_id");
    CHECK(call(writable, "edit_commit", commit).at("ok") == true);
    commit = request(writable, "second-commit");
    commit["proposal_id"] = second.at("proposal_id");
    rejected(call(writable, "edit_commit", commit), "revision_conflict");
    commit["request_key"] = "second-again";
    rejected(call(writable, "edit_commit", commit), "proposal_missing");
    CHECK(call(writable, "project_get").at("active_proposals") == 0);
}
void expiry_and_limits() {
    auto clock = Clock::now();
    Policy policy;
    policy.allow_edit = policy.allow_save = true;
    policy.max_proposals = 1;
    Session session(fixture(), policy, [](const auto &) {}, [&] { return clock; });
    initialize(session);
    const auto original = batch(session, "original");
    const auto preview = call(session, "edit_preview", original);
    rejected(call(session, "edit_preview", batch(session, "over-limit")), "proposal_limit");
    clock += std::chrono::seconds(121);
    CHECK(call(session, "edit_preview", original) == preview);
    auto commit = request(session, "expired");
    commit["proposal_id"] = preview.at("proposal_id");
    rejected(call(session, "edit_commit", commit), "proposal_missing");
    const auto before = session.current();
    auto request_before_expiry = batch(session, "session-expired");
    clock += std::chrono::hours(1);
    rejected(call(session, "edit_preview", request_before_expiry), "session_expired");
    CHECK(session.current() == before);
    policy.max_requests = 2;
    int saves = 0;
    Session bounded(fixture(), policy, [&](const auto &) { ++saves; });
    initialize(bounded);
    const auto proposal = call(bounded, "edit_preview", batch(bounded, "preview"));
    commit = request(bounded, "commit");
    commit["proposal_id"] = proposal.at("proposal_id");
    CHECK(call(bounded, "edit_commit", commit).at("ok") == true);
    rejected(call(bounded, "history_undo", request(bounded, "overflow")), "session_limit");
    const auto save = request(bounded, "checkpoint");
    CHECK(call(bounded, "project_save", save).at("ok") == true && saves == 1);
    CHECK(call(bounded, "project_save", save).at("ok") == true && saves == 1);
    CHECK(call(bounded, "edit_commit", commit).at("ok") == true);
    // The byte budget also preserves a save request when normal admission is full.
    policy.max_memo_bytes = max_payload_bytes;
    Session bytes_limited(fixture(), policy, [&](const auto &) { ++saves; });
    initialize(bytes_limited);
    rejected(call(bytes_limited, "edit_preview", batch(bytes_limited, "no-space")),
             "session_limit");
    CHECK(call(bytes_limited, "project_save", request(bytes_limited, "save-checkpoint")).at("ok") ==
          true);
    CHECK(saves == 2);
}
void protocol() {
    Session session(fixture());
    auto result = session.receive(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})");
    CHECK(result->at("error").at("code") == -32002);
    CHECK(session.receive(R"({"jsonrpc":"2.0","id":1,"method":"server/discover"})")
              ->at("error")
              .at("code") == -32601);
    CHECK(session.receive("{")->at("error").at("code") == -32700);
    CHECK(session.receive(R"({"jsonrpc":"2.0","id":1,"id":2,"method":"ping"})")
              ->at("error")
              .at("code") == -32700);
    CHECK(session.receive(std::string(100, '[') + std::string(100, ']'))->at("error").at("code") ==
          -32700);
    CHECK(session.receive("[]")->at("error").at("code") == -32600);
    CHECK(
        session.receive(R"({"jsonrpc":"2.0","id":null,"method":"ping"})")->at("error").at("code") ==
        -32600);
    CHECK(session.receive(
              R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"history_undo"}})") ==
          std::nullopt);
    initialize(session);
    result = session.receive(R"({"jsonrpc":"2.0","id":3,"method":"tools/list"})");
    CHECK(result->at("result").at("tools").size() == 9);
    CHECK(
        session.receive(R"({"jsonrpc":"2.0","id":4,"method":"unknown"})")->at("error").at("code") ==
        -32601);
    CHECK(
        session
            .receive(R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"shell"}})")
            ->at("error")
            .at("code") == -32602);
    CHECK(session.receive(R"({"jsonrpc":"2.0","id":6,"method":"ping"})")->at("result").empty());
    auto invalid = batch(session, "float-time");
    invalid["commands"][0]["frame_duration"]["value"] = 1001;
    rejected(call(session, "edit_preview", invalid), "invalid_arguments");
    invalid = batch(session, "zero-rate");
    invalid["commands"][0]["frame_duration"]["rate"] = "0";
    rejected(call(session, "edit_preview", invalid), "invalid_arguments");
    invalid = batch(session, "extra-command");
    invalid["commands"][0]["path"] = "outside";
    rejected(call(session, "edit_preview", invalid), "invalid_arguments");
}
int main() {
    try {
        equivalence();
        stale_and_permissions();
        expiry_and_limits();
        protocol();
        std::cout << "MCP lifecycle, command equivalence, permissions, revisions, retries, expiry "
                     "and limits passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
