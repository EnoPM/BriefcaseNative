#pragma once

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace briefcase::server {

struct BridgeCommand {
    enum class Kind { Kick, Kill, MoveTeam, SwapTeam, SetSpectator, ApplyEffect, SendMessage };

    Kind kind{Kind::Kick};
    std::string request_id;
    std::string player_token;
    std::string other_token;
    std::string effect_id;
    std::string message_text;
    int team_index{-1};
    int team_size{};
    int duration_seconds{};
    std::uint64_t session{};
    bool spectator{};
};

// JSON framing and validation are identical for every local bridge transport.
// The platform adapter decides which validated commands it can execute.
class BridgeProtocol final {
public:
    static std::optional<BridgeCommand> parse_command(std::string_view line) noexcept {
        if (line.size() > 4096) return std::nullopt;
        try {
            const auto value = nlohmann::json::parse(line);
            if (!value.is_object() || value.at("schemaVersion") != 2 ||
                value.at("type") != "command") return std::nullopt;
            const auto name = value.at("command").get<std::string>();
            BridgeCommand command;
            if (name == "kick") command.kind = BridgeCommand::Kind::Kick;
            else if (name == "kill") command.kind = BridgeCommand::Kind::Kill;
            else if (name == "moveTeam") command.kind = BridgeCommand::Kind::MoveTeam;
            else if (name == "swapTeam") command.kind = BridgeCommand::Kind::SwapTeam;
            else if (name == "setSpectator") command.kind = BridgeCommand::Kind::SetSpectator;
            else if (name == "applyEffect") command.kind = BridgeCommand::Kind::ApplyEffect;
            else if (name == "sendMessage") command.kind = BridgeCommand::Kind::SendMessage;
            else return std::nullopt;

            command.request_id = value.at("requestId").get<std::string>();
            command.player_token = value.at("playerToken").get<std::string>();
            if (!valid_id(command.request_id, 64, true) ||
                !valid_id(command.player_token, 64, false)) return std::nullopt;

            if (command.kind == BridgeCommand::Kind::SetSpectator)
                command.spectator = value.at("spectator").get<bool>();
            if (command.kind == BridgeCommand::Kind::SendMessage) {
                command.message_text = value.at("message").get<std::string>();
                if (command.message_text.empty() || command.message_text.size() > 1024 ||
                    std::all_of(command.message_text.begin(), command.message_text.end(),
                        [](unsigned char ch) { return ch == ' '; }) ||
                    std::any_of(command.message_text.begin(), command.message_text.end(),
                        [](unsigned char ch) { return ch < 0x20 || ch == 0x7f; }))
                    return std::nullopt;
            }
            if (command.kind == BridgeCommand::Kind::ApplyEffect) {
                command.effect_id = value.at("effectId").get<std::string>();
                command.duration_seconds = value.at("durationSeconds").get<int>();
                command.other_token = value.value("sourceToken", command.player_token);
                if (command.effect_id.empty() || command.effect_id.size() > 220 ||
                    command.duration_seconds < 1 || command.duration_seconds > 300 ||
                    !valid_id(command.other_token, 64, false) ||
                    !std::all_of(command.effect_id.begin(), command.effect_id.end(),
                        [](unsigned char ch) { return std::isalnum(ch) || ch == '_' ||
                            ch == '/' || ch == '.' || ch == ':' || ch == '-'; }))
                    return std::nullopt;
            }
            if (command.kind == BridgeCommand::Kind::MoveTeam ||
                command.kind == BridgeCommand::Kind::SwapTeam) {
                command.team_size = value.at("teamSize").get<int>();
                if (command.team_size != 2 && command.team_size != 3) return std::nullopt;
                if (command.kind == BridgeCommand::Kind::MoveTeam) {
                    command.team_index = value.at("team").get<int>();
                    if (command.team_index < 0 || command.team_index >= 32) return std::nullopt;
                } else {
                    command.other_token = value.at("otherToken").get<std::string>();
                    if (!valid_id(command.other_token, 64, false) ||
                        command.other_token == command.player_token) return std::nullopt;
                }
            }
            return command;
        } catch (...) { return std::nullopt; }
    }

    static std::string result(std::string_view request_id, bool ok, std::string_view message) {
        return nlohmann::json{{"schemaVersion", 2}, {"type", "result"},
            {"requestId", request_id}, {"ok", ok}, {"message", message}}.dump() + '\n';
    }

private:
    static bool valid_id(std::string_view value, std::size_t maximum, bool allow_dash) noexcept {
        return !value.empty() && value.size() <= maximum &&
            std::all_of(value.begin(), value.end(), [allow_dash](unsigned char ch) {
                return std::isalnum(ch) || (allow_dash && ch == '-');
            });
    }
};

} // namespace briefcase::server
