#include "mcp/wire.hpp"
namespace nle::mcp {
namespace {
Json object(Json properties, Json required) {
    return {{"type", "object"},
            {"properties", std::move(properties)},
            {"required", std::move(required)},
            {"additionalProperties", false}};
}
} // namespace
Json tool_catalog() {
    const Json str{{"type", "string"}, {"minLength", 1}, {"maxLength", 4096}};
    const Json integer{{"type", "string"}, {"pattern", "^(0|[1-9][0-9]*)$"}, {"maxLength", 20}};
    const Json positive{{"type", "string"}, {"pattern", "^[1-9][0-9]*$"}, {"maxLength", 20}};
    const Json reference{{"type", "string"},
                         {"pattern", "^([1-9][0-9]*|\\$[A-Za-z][A-Za-z0-9_]{0,63})$"},
                         {"maxLength", 65}};
    const Json alias{
        {"type", "string"}, {"pattern", "^[A-Za-z][A-Za-z0-9_]{0,63}$"}, {"maxLength", 64}};
    const auto rational = object({{"value", integer}, {"rate", positive}}, {"value", "rate"});
    const Json stream_choice{
        {"oneOf",
         Json::array(
             {object({{"mode", {{"enum", {"auto", "disabled"}}}}}, {"mode"}),
              object({{"mode", {{"const", "stream"}}},
                      {"index", {{"type", "integer"}, {"minimum", 0}, {"maximum", 4294967295ULL}}}},
                     {"mode", "index"})})}};
    const auto routing =
        object({{"video", stream_choice}, {"audio", stream_choice}}, {"video", "audio"});
    const auto output = object(
        {{"width", {{"type", "integer"}, {"minimum", 2}, {"maximum", 3840}, {"multipleOf", 2}}},
         {"height", {{"type", "integer"}, {"minimum", 2}, {"maximum", 2160}, {"multipleOf", 2}}},
         {"sample_rate", {{"const", 48000}}},
         {"channels", {{"const", 2}}}},
        {"width", "height", "sample_rate", "channels"});
    Json actions = Json::array();
    const auto action = [&](const std::string &name, Json properties, Json required,
                            bool returns_id = false) {
        properties["op"] = {{"const", name}};
        required.push_back("op");
        if (returns_id)
            properties["as"] = alias;
        actions.push_back(object(std::move(properties), std::move(required)));
    };
    action("create_sequence", {{"name", str}, {"frame_duration", rational}}, {"name"}, true);
    action("create_track",
           {{"sequence_id", reference}, {"name", str}, {"kind", {{"enum", {"video", "audio"}}}}},
           {"sequence_id", "name", "kind"}, true);
    action("insert_clip",
           {{"track_id", reference},
            {"media_id", reference},
            {"position", rational},
            {"source_in", rational},
            {"duration", rational},
            {"routing", routing}},
           {"track_id", "media_id", "position", "source_in", "duration"}, true);
    action("set_sequence_output",
           {{"sequence_id", reference}, {"frame_duration", rational}, {"output", output}},
           {"sequence_id", "frame_duration", "output"});
    action("set_track_playback",
           {{"track_id", reference},
            {"enabled", {{"type", "boolean"}}},
            {"muted", {{"type", "boolean"}}},
            {"gain_milli", {{"type", "integer"}, {"minimum", 0}, {"maximum", 4000}}}},
           {"track_id", "enabled", "muted", "gain_milli"});
    action("set_clip_routing", {{"clip_id", reference}, {"routing", routing}},
           {"clip_id", "routing"});
    action("move_clip", {{"clip_id", reference}, {"track_id", reference}, {"position", rational}},
           {"clip_id", "track_id", "position"});
    action("trim_clip",
           {{"clip_id", reference},
            {"position", rational},
            {"source_in", rational},
            {"duration", rational}},
           {"clip_id", "position", "source_in", "duration"});
    action("split_clip", {{"clip_id", reference}, {"position", rational}}, {"clip_id", "position"},
           true);
    action("delete_clip", {{"clip_id", reference}}, {"clip_id"});
    action("delete_track", {{"track_id", reference}}, {"track_id"});
    action("reorder_track",
           {{"sequence_id", reference}, {"track_id", reference}, {"index", integer}},
           {"sequence_id", "track_id", "index"});
    Json tools = Json::array();
    const auto add = [&](const std::string &name, const std::string &description, Json properties,
                         Json required, bool read_only, bool session_request = false) {
        if (session_request) {
            properties["session_id"] = str;
            properties["project_id"] = positive;
            properties["expected_revision"] = integer;
            properties["request_key"] = {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}};
            for (const auto *key : {"session_id", "project_id", "expected_revision", "request_key"})
                required.push_back(key);
        }
        tools.push_back({{"name", name},
                         {"description", description},
                         {"inputSchema", object(properties, required)},
                         {"outputSchema",
                          {{"type", "object"},
                           {"properties", {{"ok", {{"type", "boolean"}}}}},
                           {"required", {"ok"}}}},
                         {"annotations",
                          {{"readOnlyHint", read_only},
                           {"destructiveHint", !read_only},
                           {"idempotentHint", true},
                           {"openWorldHint", false}}}});
    };
    add("project_get",
        "Get the configured project identity, current revision, session ID, permissions and "
        "history status. Start here.",
        Json::object(), Json::array(), true);
    add("project_snapshot",
        "Inspect detached media and timeline state. IDs and times are exact strings. No media "
        "files are read.",
        {{"project_id", positive}}, {"project_id"}, true);
    add("project_changes",
        "Read at most 100 audit records after a revision. Continue with next_revision until "
        "has_more is false.",
        {{"project_id", positive},
         {"since_revision", integer},
         {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}}},
        {"project_id", "since_revision"}, true);
    add("edit_preview",
        "Propose 1-128 commands as one detached transaction. Use as aliases and $alias references "
        "for new IDs. Inspect the provisional snapshot before committing. No live edit or save "
        "occurs.",
        {{"label", str},
         {"commands",
          {{"type", "array"},
           {"minItems", 1},
           {"maxItems", 128},
           {"items", {{"oneOf", actions}}}}}},
        {"label", "commands"}, true, true);
    add("edit_commit",
        "Commit an unexpired preview as one undoable edit. Requires launch-time edit permission "
        "and the current expected revision. Does not save the file.",
        {{"proposal_id", str}}, {"proposal_id"}, false, true);
    add("edit_rollback", "Discard a proposal without editing the project.", {{"proposal_id", str}},
        {"proposal_id"}, true, true);
    add("history_undo",
        "Undo the last retained edit using agent attribution. Requires edit permission. Does not "
        "save.",
        {{"label", str}}, Json::array(), false, true);
    add("history_redo",
        "Redo the last undone edit using agent attribution. Requires edit permission. Does not "
        "save.",
        {{"label", str}}, Json::array(), false, true);
    add("project_save",
        "Save to the single launcher-selected file. Requires save permission and no external "
        "changes since load/last save. No path or Save As argument is accepted.",
        Json::object(), Json::array(), false, true);
    return tools;
}
} // namespace nle::mcp
