// Opt-in integration probe. Never installed, never run by CTest.
// Arguments: server Briefcase, client Briefcase, protected password JSON, game endpoint.
// Reads status and validates same-value saves through the production asynchronous client.
#include "../runtime/Briefcase.Client.Admin/Client.hpp"
#include <Windows.h>
#include <iostream>
#include <wincrypt.h>
using namespace bc::admin;
static void require(bool ok, const char *what) {
    if (!ok)
        throw std::runtime_error(what);
}
static Json wait(Client &client, uint64_t &sequence) {
    const auto end = Clock::now() + std::chrono::seconds(15);
    std::string text;
    while (Clock::now() < end) {
        if (client.snapshot(text, sequence)) {
            auto state = Json::parse(text);
            if (!state.value("pending", true))
                return state;
        }
        Sleep(5);
    }
    throw std::runtime_error("Client operation timeout.");
}
int main(int argc, char **argv) {
    try {
        require(argc == 5 ||
                    (argc == 6 && (std::string(argv[5]) == "--restart" || std::string(argv[5]) == "--idle")),
                "Expected server Briefcase, client Briefcase, protected password JSON, game endpoint.");
        fs::path server = fs::absolute(argv[1]).lexically_normal();
        fs::path root = fs::absolute(argv[2]).lexically_normal();
        require(server.filename() == "Briefcase" && root.filename() == "Briefcase" &&
                    fs::exists(server.parent_path() / "DeceiveIncServer-Win64-Shipping.exe") &&
                    fs::exists(root.parent_path() / "DeceiveInc-Win64-Shipping.exe"),
                "Invalid test copies.");
        auto pairing = Json::parse(read_file(server / "Admin/pairing.json"));
        auto encrypted = unhex(Json::parse(read_file(argv[3])).at("protectedPassword").get<std::string>());
        DATA_BLOB input{DWORD(encrypted.size()), encrypted.data()}, plain{};
        require(
            CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &plain),
            "Cannot decrypt local password.");
        struct WipeBlob {
            DATA_BLOB &blob;
            ~WipeBlob() {
                SecureZeroMemory(blob.pbData, blob.cbData);
                LocalFree(blob.pbData);
            }
        } wipeBlob{plain};
        std::string password(reinterpret_cast<char *>(plain.pbData), plain.cbData);
        struct Wipe {
            std::string &s;
            ~Wipe() { erase(s); }
        } wipe{password};
        Client client(root);
        uint64_t sequence{};
        require(client.connect(1, "Local integration test", argv[4], pairing.at("endpoint"),
                               pairing.at("fingerprint"), "Deliberately-invalid-password"),
                "Queue invalid login.");
        auto bad = wait(client, sequence);
        require(bad.at("state") == "error" && bad.value("errorCode", "") == "unauthorized",
                "Incorrect password was not rejected.");
        const auto start = Clock::now();
        require(client.connect(1, "Local integration test", argv[4], pairing.at("endpoint"),
                               pairing.at("fingerprint"), password, true),
                "Queue login.");
        erase(password);
        auto ready = wait(client, sequence);
        const auto login = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        if (ready.at("state") != "ready") {
            std::cerr << ready.value("message", "No error detail") << "\n";
            throw std::runtime_error("Live server login failed.");
        }
        require(ready.at("server").at("loadedMods") == 3 && ready.at("configs").size() == 3,
                "Expected exactly three loaded editable mods.");
        Json report{{"loginAndInventoryMs", login}, {"wrongPasswordRejected", true},
                    {"server", ready.at("server")}, {"mods", ready.at("mods")},
                    {"tls", ready.at("tls")},       {"configs", ready.at("configs")}};
        unsigned saves{};
        for (auto it = report["configs"].begin(); it != report["configs"].end(); ++it) {
            const auto &before = it.value();
            require(before.at("saved") == before.at("active"),
                    "Pending settings already exist; no test write allowed.");
            require(client.submit("save", {{"modId", it.key()},
                                           {"expectedRevision", before.at("revision")},
                                           {"values", before.at("saved")}}),
                    "Queue same-value save.");
            auto after = wait(client, sequence);
            require(after.at("state") == "ready" && !after.contains("errorCode"), "Same-value save failed.");
            require(after.at("configs").at(it.key()) == before, "Settings changed during same-value save.");
            auto disk = Json::parse(read_file(server / "Mods" / it.key() / "Data/config.json"));
            require(disk == before.at("saved"), "Persisted settings differ from original.");
            ++saves;
        }
        const auto refreshStart = Clock::now();
        require(client.submit("refresh"), "Queue refresh.");
        auto refreshed = wait(client, sequence);
        require(refreshed.at("configs") == report.at("configs"), "Settings changed after refresh.");
        report["refreshAndInventoryMs"] =
            std::chrono::duration<double, std::milli>(Clock::now() - refreshStart).count();
        report["sameValueSavesVerified"] = saves;
        auto command = [&](const std::string &op, const Json &payload) {
            Sleep(60);
            require(client.submit(op, payload), "Management command enqueue");
            auto state = wait(client, sequence);
            if (state.contains("errorCode"))
                throw std::runtime_error(op + ": " + state.value("message", std::string{}));
            return state;
        };
        require(ready["server"].value("administrationVersion", 0) >= 2, "Management extension not deployed");
        auto config = ready["serverConfig"];
        auto configured = command("server.config.write",
                                  {{"expectedRevision", config["revision"]}, {"values", config["saved"]}});
        require(configured["serverConfig"] == config, "Same-value server config changed");
        auto selection = ready["selection"];
        auto selected = command("mods.selection.write", {{"expectedRevision", selection["revision"]},
                                                         {"enabledMods", selection["saved"]}});
        require(selected["selection"] == selection, "Same-value selection changed");
        report["serverConfigurationFields"] = config["saved"].size();
        report["selectionPreserved"] = true;
        Json groups = Json::object();
        for (auto it = ready["balance"]["groups"].begin(); it != ready["balance"]["groups"].end(); ++it) {
            auto balance = command("balance.read", {{"group", it.key()}})["balance"];
            require(balance["entries"].size() == it.value(), "Character grouping lost entries");
            require(balance.dump().size() < 1048576, "Group response too large");
            groups[it.key()] = balance["entries"].size();
        }
        report["balanceGroups"] = groups;
        auto balance = command("balance.read", {{"group", "Ace"}})["balance"];
        auto same = command(
            "balance.write",
            {{"group", "Ace"}, {"expectedRevision", balance["revision"]}, {"changes", Json::array()}});
        require(same["balance"] == balance, "Empty balance save changed profile");
        for (auto source : {"framework", "game"}) {
            auto logs = command("server.logs", {{"source", source}})["logs"];
            report["logs"][source] = {{"characters", logs["text"].get_ref<const std::string &>().size()},
                                      {"truncated", logs["truncated"]}};
        }
        client.disconnect();
        require(wait(client, sequence).at("state") == "idle", "Disconnect failed.");
        require(client.select(1, "Local integration test", argv[4]),
                "Saved password automatic selection enqueue");
        auto remembered = wait(client, sequence);
        require(remembered.at("state") == "ready" && remembered.value("passwordSaved", false),
                "Remembered login failed");
        report["rememberedPasswordReconnect"] = true;
        report["automaticLoginOnSelection"] = true;
        report["translationsRead"] = remembered.contains("translations");
        if (argc == 6 && std::string(argv[5]) == "--idle") {
            auto idleStart = Clock::now();
            Sleep(96000);
            require(client.submit("refresh"), "Idle session refresh rejected");
            auto afterIdle = wait(client, sequence);
            require(afterIdle["state"] == "ready" && afterIdle["serverId"] == remembered["serverId"],
                    "Session disconnected after 90 seconds");
            report["idleSecondsVerified"] = std::chrono::duration<double>(Clock::now() - idleStart).count();
            report["sameServerAfterIdle"] = true;
        }
        if (argc == 6 && std::string(argv[5]) == "--restart") {
            const auto previous = remembered["server"].at("processId").get<DWORD>();
            auto restarting = command("server.restart", Json::object());
            require(restarting["state"] == "idle" && restarting["restart"]["scheduled"] == true,
                    "Restart not acknowledged");
            Json restartResult;
            const auto deadline = Clock::now() + std::chrono::seconds(45);
            while (Clock::now() < deadline) {
                Sleep(250);
                try {
                    restartResult = Json::parse(read_file(server / "Admin/restart-result.json"));
                    if (restartResult.value("requestId", "") ==
                            restarting["restart"]["requestId"].get<std::string>() &&
                        restartResult.value("state", "") == "started")
                        break;
                } catch (...) {
                }
            }
            require(restartResult.value("state", "") == "started" &&
                        restartResult.value("previousPid", DWORD{}) == previous,
                    "Server restart helper failed");
            require(restartResult["workingDirectory"] == server.parent_path().string(),
                    "Restart working directory mismatch");
            Json after;
            while (Clock::now() < deadline) {
                Sleep(1000);
                require(client.connect(1, "Local integration test", argv[4], pairing.at("endpoint"),
                                       pairing.at("fingerprint"), "", true),
                        "Restart reconnect enqueue");
                after = wait(client, sequence);
                if (after["state"] == "ready")
                    break;
            }
            require(after["state"] == "ready" && after["server"].at("processId") != previous,
                    "Restart did not create a new authenticated server");
            require(after["configs"] == report["configs"] &&
                        after["selection"]["saved"] == selection["saved"],
                    "Restart changed mods or their settings");
            report["restart"] = {{"previousPid", previous},
                                 {"pid", after["server"]["processId"]},
                                 {"workingDirectory", restartResult["workingDirectory"]},
                                 {"reconnected", true}};
        }
        client.disconnect();
        require(wait(client, sequence).at("state") == "idle", "Final disconnect failed.");
        client.stop();
        report["disconnectedCleanly"] = true;
        std::cout << report.dump(2) << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Live administration probe failed: " << e.what() << "\n";
        return 1;
    }
}
