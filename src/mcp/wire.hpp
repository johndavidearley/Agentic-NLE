#pragma once
#include "commands/editor.hpp"
#include <map>
#include <nlohmann/json.hpp>
#include <string_view>
namespace nle::mcp {
using Json = nlohmann::json;
inline constexpr std::size_t max_message_bytes = 1024 * 1024;
inline constexpr std::size_t max_payload_bytes = 2 * 1024 * 1024;
struct Failure : std::runtime_error {
    std::string code;
    Failure(std::string error_code, std::string message)
        : std::runtime_error(std::move(message)), code(std::move(error_code)) {}
};
void fields(const Json &value, std::initializer_list<std::string_view> required,
            std::initializer_list<std::string_view> optional = {});
std::string text(const Json &value, std::size_t limit = 4096);
std::uint64_t number(const Json &value, bool zero = true);
RationalTime time(const Json &value);
Json time(RationalTime value);
Json snapshot(const ProjectSnapshot &project);
Json operation(const OperationRecord &value);
Json command_result(const CommandResult &value);
using Aliases = std::map<std::string, CommandResult>;
Command command(const Json &value, const Aliases &aliases);
Json tool_catalog();
Json parse(std::string_view value);
} // namespace nle::mcp
