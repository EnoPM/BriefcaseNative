#include "../ServerBridgeProtocol.hpp"
#include "../ServerBridgeSnapshot.hpp"
#include "../ServerBridgeChannel.hpp"
#include <string_view>

int main() {
    using briefcase::server::BridgeCommand;
    using briefcase::server::BridgeProtocol;
    auto parse = [](std::string_view message) { return BridgeProtocol::parse_command(message); };

    const auto kick = parse(R"({"schemaVersion":2,"type":"command","command":"kick","requestId":"req-1","playerToken":"p1"})");
    if (!kick || kick->kind != BridgeCommand::Kind::Kick || kick->request_id != "req-1") return 1;
    const auto move = parse(R"({"schemaVersion":2,"type":"command","command":"moveTeam","requestId":"req-2","playerToken":"p1","teamSize":2,"team":3})");
    if (!move || move->kind != BridgeCommand::Kind::MoveTeam || move->team_index != 3) return 2;
    const auto swap = parse(R"({"schemaVersion":2,"type":"command","command":"swapTeam","requestId":"req-3","playerToken":"p1","otherToken":"p2","teamSize":3})");
    if (!swap || swap->kind != BridgeCommand::Kind::SwapTeam || swap->other_token != "p2") return 3;
    const auto effect = parse(R"({"schemaVersion":2,"type":"command","command":"applyEffect","requestId":"req-4","playerToken":"p1","effectId":"/Game/Effect.Name","durationSeconds":10})");
    if (!effect || effect->kind != BridgeCommand::Kind::ApplyEffect ||
        effect->other_token != "p1") return 4;
    const auto message = parse(R"({"schemaVersion":2,"type":"command","command":"sendMessage","requestId":"req-5","playerToken":"p2","message":"Bonjour, agent !"})");
    if (!message || message->kind != BridgeCommand::Kind::SendMessage ||
        message->player_token != "p2" || message->message_text != "Bonjour, agent !") return 15;
    const auto spectator = parse(R"({"schemaVersion":2,"type":"command","command":"setSpectator","requestId":"req-6","playerToken":"p1","spectator":true})");
    if (!spectator || spectator->kind != BridgeCommand::Kind::SetSpectator ||
        !spectator->spectator) return 17;
    if (parse(R"({"schemaVersion":2,"type":"command","command":"sendMessage","requestId":"req-5","playerToken":"p2","message":"   "})") ||
        parse(R"({"schemaVersion":2,"type":"command","command":"sendMessage","requestId":"req-5","playerToken":"p2","message":"hello\nworld"})")) return 16;

    if (parse(R"({"schemaVersion":3,"type":"command","command":"kick","requestId":"req-1","playerToken":"p1"})") ||
        parse(R"({"schemaVersion":2,"type":"command","command":"moveTeam","requestId":"req-2","playerToken":"p1","teamSize":2,"team":32})") ||
        parse(R"({"schemaVersion":2,"type":"command","command":"swapTeam","requestId":"req-3","playerToken":"p1","otherToken":"p1","teamSize":3})") ||
        parse(R"({"schemaVersion":2,"type":"command","command":"kick","requestId":"req/1","playerToken":"p1"})")) return 5;

    const auto result = nlohmann::json::parse(BridgeProtocol::result("req-1", true, "Kick sent"));
    if (result.at("schemaVersion") != 2 || result.at("requestId") != "req-1" ||
        result.at("ok") != true || result.at("message") != "Kick sent") return 6;

    briefcase::server::BridgeSnapshot snapshots;
    std::uint64_t sent{};
    std::chrono::steady_clock::time_point heartbeat{};
    snapshots.publish("first\n");
    if (snapshots.next(sent, heartbeat) != "first\n" ||
        !snapshots.next(sent, heartbeat).empty()) return 7;
    snapshots.publish("first\n");
    if (!snapshots.next(sent, heartbeat).empty()) return 8;
    snapshots.publish("second\n");
    if (snapshots.next(sent, heartbeat) != "second\n") return 9;

    briefcase::server::BridgeCommandChannel channel;
    channel.reset_session();
    channel.receive(R"({"schemaVersion":2,"type":"command","command":"kick","requestId":"req-1","playerToken":"p1"})",
        briefcase::server::BridgeCommandSet::LocalBasic);
    if (!channel.has_pending()) return 10;
    auto commands = channel.take_commands();
    if (commands.size() != 1 || channel.has_pending()) return 11;
    channel.reply(commands.front(), true, "Kick sent");
    if (channel.take_responses().size() != 1) return 12;
    channel.reset_session();
    channel.reply(commands.front(), true, "Late reply");
    if (!channel.take_responses().empty()) return 13;
    channel.receive(R"({"schemaVersion":2,"type":"command","command":"applyEffect","requestId":"req-4","playerToken":"p1","effectId":"/Game/Effect.Name","durationSeconds":10})",
        briefcase::server::BridgeCommandSet::LocalBasic);
    if (channel.has_pending()) return 14;
    channel.receive(R"({"schemaVersion":2,"type":"command","command":"setSpectator","requestId":"req-6","playerToken":"p1","spectator":true})",
        briefcase::server::BridgeCommandSet::LocalBasic);
    if (!channel.has_pending() || channel.take_commands().front().kind !=
        BridgeCommand::Kind::SetSpectator) return 18;
}
