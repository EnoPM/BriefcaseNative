#include "Client.hpp"
#include "../Briefcase.Admin/Management.hpp"
#include "../Briefcase.Client.Servers/ServerDirectory.hpp"
#include "../Briefcase.Localization/Catalog.hpp"
#include "../Briefcase.NativeHost/Configuration.hpp"
#include "../Briefcase.NativeHost/Startup.hpp"
#include <set>
namespace bc::admin {
Client::Client(fs::path root, std::chrono::milliseconds heartbeat)
    : heartbeat_interval(heartbeat), root(std::move(root)), passwords(this->root) {}
Client::~Client() {
    stop();
}
void Client::publish() {
    const auto message = state.value("message", std::string{});
    std::string kind = "info", key;
    if (state.contains("errorCode")) {
        auto code = state["errorCode"].get<std::string>();
        kind = code == "conflict" ? "warning" : "danger";
        key = "ui.error." + code;
    } else if (message == "Data refreshed.") {
        kind = "success";
        key = "ui.message.refreshed";
    } else if (message.starts_with("Settings saved.") || message.starts_with("Saved.")) {
        kind = "success";
        key = "ui.message.saved";
    } else if (message == "Disconnected.")
        key = "ui.message.disconnected";
    else if (message == "Password forgotten on this client.") {
        kind = "success";
        key = "ui.message.forgotten";
    } else if (message.starts_with("Restart requested")) {
        kind = "warning";
        key = "ui.message.restarting";
    } else if (message.starts_with("Connected. Identity")) {
        kind = "warning";
        key = "ui.message.store_warning";
    } else if (state.value("state", "") == "connecting")
        key = "ui.message.connecting";
    else if (state.value("state", "") == "error") {
        kind = "danger";
        key = "ui.message.connection_failed";
    }
    state["messageKind"] = kind;
    state["messageKey"] = key;
    serialized = state.dump();
    ++sequence;
}
bool Client::snapshot(std::string &out, uint64_t &version) {
    std::lock_guard lock(mutex);
    if (version == sequence)
        return false;
    out = serialized;
    version = sequence;
    return true;
}
bool Client::enqueue(Command next) {
    std::lock_guard lock(mutex);
    if (quitting || command || state.value("pending", false))
        return false;
    if (!worker)
        worker = std::make_unique<std::thread>([this] { run(); });
    if (next.operation == "connect" || next.operation == "select")
        cancelled = false;
    command.emplace(std::move(next));
    state["pending"] = true;
    state["message"] = "";
    state.erase("errorCode");
    publish();
    changed.notify_one();
    return true;
}
bool Client::select(uint64_t id, const std::string &name, const std::string &game_endpoint) {
    Command c;
    c.operation = "select";
    c.favorite = id;
    c.name = name;
    c.game_endpoint = game_endpoint;
    return enqueue(std::move(c));
}
bool Client::connect(uint64_t id, const std::string &name, const std::string &game_endpoint,
                     const std::string &endpoint, const std::string &pin, const std::string &password,
                     bool remember) {
    auto normalized = servers::normalize_endpoint(endpoint);
    auto bytes = unhex(pin);
    if (bytes.size() != 32 || (!password.empty() && password.size() < 12) || password.size() > 256)
        return false;
    Command c;
    c.operation = "connect";
    c.favorite = id;
    c.name = name;
    c.game_endpoint = game_endpoint;
    c.endpoint = normalized;
    c.pin = hex(bytes);
    c.password = password;
    c.remember = remember;
    return enqueue(std::move(c));
}
bool Client::submit(const std::string &op, const Json &p) {
    static const std::set<std::string> operations{"refresh",
                                                  "save",
                                                  "forget",
                                                  "server.logs",
                                                  "server.restart",
                                                  "server.config.read",
                                                  "server.config.write",
                                                  "mods.selection.read",
                                                  "mods.selection.write",
                                                  "balance.read",
                                                  "balance.write"};
    if (!operations.contains(op))
        return false;
    {
        std::lock_guard lock(mutex);
        if (op != "forget" && state.value("state", "") != "ready")
            return false;
    }
    Command c;
    c.operation = op;
    c.payload = p;
    return enqueue(std::move(c));
}
void Client::disconnect() {
    std::lock_guard lock(mutex);
    cancelled = true;
    if (!worker) {
        state["state"] = "idle";
        state["pending"] = false;
        publish();
        return;
    }
    Command c;
    c.operation = "disconnect";
    command.emplace(std::move(c));
    state["pending"] = true;
    publish();
    changed.notify_one();
}
void Client::request_stop() noexcept {
    quitting = true;
    cancelled = true;
    changed.notify_all();
}
void Client::stop() noexcept {
    request_stop();
    if (worker && worker->joinable())
        worker->join();
    worker.reset();
}
void Client::load_pairing(const Command &c) {
    Json pairings = Json::object();
    const auto file = root / "Admin" / "clients.json";
    if (fs::exists(file)) {
        pairings = bc::strict_json(read_file(file));
        if (!pairings.is_object() || pairings.size() > 64)
            throw Error("pairing_file", "Invalid identity file.");
    }
    selected_game = c.game_endpoint;
    Json pairing = pairings.value(c.game_endpoint, Json::object());
    std::string endpoint, pin;
    if (pairing.contains("endpoint"))
        endpoint = servers::normalize_endpoint(pairing.at("endpoint").get<std::string>());
    if (pairing.contains("fingerprint")) {
        auto bytes = unhex(pairing.at("fingerprint").get<std::string>());
        if (bytes.size() != 32)
            throw Error("pairing_file", "Invalid saved fingerprint.");
        pin = hex(bytes);
    }
    if (endpoint.empty()) {
        auto normalized = servers::normalize_endpoint(c.game_endpoint);
        auto colon = normalized.rfind(':');
        endpoint = normalized.substr(0, colon + 1) + "50002";
    }
    std::lock_guard lock(mutex);
    state = {{"state", "idle"},
             {"pending", true},
             {"favoriteId", c.favorite},
             {"name", c.name},
             {"endpoint", endpoint},
             {"fingerprint", pin},
             {"message", ""},
             {"passwordSaved", passwords.contains(c.game_endpoint, endpoint, pin)}};
    publish();
}
void Client::save_pairing(const Command &c) {
    const auto directory = root / "Admin";
    assert_plain_path(directory);
    fs::create_directories(directory);
    const auto file = directory / "clients.json";
    Json pairings = Json::object();
    if (fs::exists(file))
        pairings = bc::strict_json(read_file(file));
    if (!pairings.is_object() || pairings.size() > 64 ||
        (!pairings.contains(c.game_endpoint) && pairings.size() >= 64))
        throw Error("pairing_file", "The identity list is full or invalid.");
    pairings[c.game_endpoint] = {{"endpoint", c.endpoint}, {"fingerprint", c.pin}};
    write_file(file, pairings.dump(2) + "\n");
}
static void validate_remote(const Json &status, const Json &mods, const Json &configs) {
    auto text = [](const Json &j, const char *field, size_t limit) {
        if (!j.contains(field) || !j[field].is_string() ||
            j[field].get_ref<const std::string &>().size() > limit)
            throw Error("protocol_error", "Invalid remote text.");
    };
    for (auto field : {"framework", "gameBuild", "unreal"})
        text(status, field, 128);
    for (auto field : {"backendState", "uptimeSeconds", "loadedMods", "discoveredMods"})
        if (!status.contains(field) || !status[field].is_number_integer() || status[field] < 0 ||
            status[field] > INT64_MAX)
            throw Error("protocol_error", "Invalid remote numeric state.");
    if (status["backendState"] > 3 || status["loadedMods"] > 128 || status["discoveredMods"] > 128)
        throw Error("protocol_error", "Remote state is out of range.");
    std::set<std::string> ids;
    for (const auto &mod : mods) {
        if (!mod.is_object())
            throw Error("protocol_error", "Invalid remote mod.");
        text(mod, "id", 128);
        text(mod, "name", 256);
        text(mod, "version", 32);
        text(mod, "author", 128);
        text(mod, "error", 512);
        if (!ids.insert(mod["id"].get<std::string>()).second)
            throw Error("protocol_error", "Duplicate mod identifier.");
        if (!mod.contains("state") || !mod["state"].is_number_integer() || mod["state"] < 0 ||
            mod["state"] > 3 || !mod.contains("editable") || !mod["editable"].is_boolean() ||
            !mod.contains("dependencies") || !mod["dependencies"].is_array() ||
            mod["dependencies"].size() > 128)
            throw Error("protocol_error", "Invalid mod metadata.");
        for (const auto &dep : mod["dependencies"]) {
            text(dep, "id", 128);
            text(dep, "minimum", 32);
        }
    }
    for (auto it = configs.begin(); it != configs.end(); ++it) {
        const auto &config = it.value();
        text(config, "revision", 64);
        text(config, "modId", 128);
        if (config["revision"].get_ref<const std::string &>().size() != 64 || config["modId"] != it.key() ||
            !config.at("restartRequired").is_boolean())
            throw Error("protocol_error", "Invalid configuration revision.");
        const auto &schema = config.at("schema"), &props = schema.at("properties");
        if (!props.is_object() || props.size() > 8)
            throw Error("protocol_error", "Remote schema is too large.");
        for (auto prop = props.begin(); prop != props.end(); ++prop) {
            locale::validate_presentation(prop.value());
            auto type = prop.value().at("type").get<std::string>();
            if (prop.key().size() > 64 || (type != "integer" && type != "number" && type != "boolean"))
                throw Error("protocol_error", "Unsupported setting type.");
        }
        for (auto key : {"active", "saved"}) {
            const auto normalized = normalize_config(schema, config.at(key));
            if (normalized != config.at(key))
                throw Error("protocol_error", "Incomplete remote settings.");
            for (auto value = normalized.begin(); value != normalized.end(); ++value)
                if (props.at(value.key()).at("type") == "integer" &&
                    (value.value() < INT_MIN || value.value() > INT_MAX))
                    throw Error("protocol_error", "Remote integer is too large.");
        }
    }
}
static void validate_extra(const std::string &op, const Json &j) {
    if (op.starts_with("server.config.")) {
        auto validation = j.at("schema"), expected = Management::config_schema();
        auto strip = [](Json &schema) {
            for (auto &prop : schema.at("properties"))
                for (auto key : {"displayName", "displayNameKey", "category", "categoryLabel", "categoryKey",
                                 "descriptionKey", "description"}) {
                    if (prop.contains(key) &&
                        (!prop[key].is_string() || prop[key].get_ref<const std::string &>().size() > 256))
                        throw Error("protocol_error", "Invalid remote metadata.");
                    prop.erase(key);
                }
        };
        strip(validation);
        strip(expected);
        if (validation != expected)
            throw Error("protocol_error", "Incompatible server schema.");
        for (auto k : {"saved", "active"})
            if (normalize_config(Management::config_schema(), j.at(k)) != j.at(k))
                throw Error("protocol_error", "Invalid remote configuration.");
        if (!j.at("restartRequired").is_boolean() || !j.at("revision").is_string() ||
            j["revision"].get_ref<const std::string &>().size() != 64)
            throw Error("protocol_error", "Invalid revision.");
    } else if (op.starts_with("mods.selection.")) {
        for (auto key : {"saved", "active"}) {
            if (!j.at(key).is_array() || j[key].size() > 128)
                throw Error("protocol_error", "Invalid remote selection.");
            for (auto &id : j[key])
                if (!id.is_string() || id.get_ref<const std::string &>().size() > 128)
                    throw Error("protocol_error", "Invalid identifier.");
        }
        if (!j.at("restartRequired").is_boolean() || !j.at("revision").is_string())
            throw Error("protocol_error", "Invalid selection.");
    } else if (op.starts_with("balance.")) {
        auto text = [](const Json &v, size_t n) {
            if (!v.is_string() || v.get_ref<const std::string &>().size() > n)
                throw Error("protocol_error", "Invalid balancing text.");
        };
        text(j.at("group"), 128);
        text(j.at("revision"), 64);
        if (!j.at("groups").is_object() || j["groups"].size() > 32 || !j.at("entries").is_array() ||
            j["entries"].size() > 512 || !j.at("restartRequired").is_boolean() ||
            !j.at("available").is_boolean())
            throw Error("protocol_error", "Invalid remote profile.");
        for (auto it = j["groups"].begin(); it != j["groups"].end(); ++it)
            if (it.key().size() > 128 || !it.value().is_number_unsigned() || it.value() > 2048)
                throw Error("protocol_error", "Invalid remote group.");
        if (j.contains("groupLabels")) {
            if (!j["groupLabels"].is_object() || j["groupLabels"].size() > 32)
                throw Error("protocol_error", "Invalid group labels");
            for (auto &label : j["groupLabels"])
                locale::validate_presentation(label);
        }
        std::set<std::string> ids;
        for (auto &x : j["entries"]) {
            for (auto name : {"presentation", "rowPresentation"})
                if (x.contains(name))
                    locale::validate_presentation(x[name]);
            for (auto k : {"id", "table", "row", "field"})
                text(x.at(k), 128);
            if (!ids.insert(x["id"].get<std::string>()).second || !x.at("editable").is_boolean())
                throw Error("protocol_error", "Duplicate or invalid setting.");
            if (x.contains("info"))
                text(x["info"], 2048);
            if (x.contains("allowedRange"))
                text(x["allowedRange"], 128);
            for (auto k : {"saved", "active", "default"})
                if (!(x.at(k).is_boolean() || (x[k].is_number() && std::isfinite(x[k].get<double>()) &&
                                               std::abs(x[k].get<double>()) <= 100000000)))
                    throw Error("protocol_error", "Invalid remote value.");
        }
    } else if (op == "server.logs") {
        if (!j.at("text").is_string() || j["text"].get_ref<const std::string &>().size() > 262144 ||
            !j.at("source").is_string() || !j.at("truncated").is_boolean())
            throw Error("protocol_error", "Invalid remote log.");
    } else if (op == "server.restart") {
        if (!j.at("scheduled").is_boolean() || !j["scheduled"].get<bool>())
            throw Error("protocol_error", "Restart not confirmed.");
    }
}
static std::string section_for(const std::string &op) {
    if (op.starts_with("server.config."))
        return "serverConfig";
    if (op.starts_with("mods.selection."))
        return "selection";
    if (op.starts_with("balance."))
        return "balance";
    if (op == "server.logs")
        return "logs";
    return "restart";
}
void Client::refresh(Connection &c) {
    auto status = c.request("server.status"), mods = c.request("mods.list");
    if (!status.is_object() || !mods.is_array() || mods.size() > 128)
        throw Error("protocol_error", "Invalid server state.");
    Json configs = Json::object();
    for (const auto &mod : mods) {
        if (mod.value("editable", false)) {
            auto id = mod.at("id").get<std::string>();
            if (!ConfigStore::allowed(id))
                throw Error("protocol_error", "Unsupported remote settings.");
            configs[id] = c.request("mod.config.read", {{"modId", id}});
        }
    }
    validate_remote(status, mods, configs);
    Json extras = Json::object();
    if (status.value("administrationVersion", 0) >= 2) {
        for (auto op : {"server.config.read", "mods.selection.read", "balance.read"}) {
            auto value = c.request(op);
            validate_extra(op, value);
            extras[section_for(op)] = std::move(value);
        }
    }
    if (status.value("administrationVersion", 0) >= 3) {
        auto catalogues = c.request("translations.read");
        locale::validate_catalogues(catalogues, true);
        extras["translations"] = std::move(catalogues);
    }
    std::lock_guard lock(mutex);
    for (auto key : {"translations", "serverConfig", "selection", "balance", "logs", "restart"})
        state.erase(key);
    state.update(extras);
    state["server"] = std::move(status);
    state["mods"] = std::move(mods);
    state["configs"] = std::move(configs);
    state["tls"] = c.protocol;
    state["serverId"] = c.server_id;
    state["state"] = "ready";
    state["message"] = "Data refreshed.";
    publish();
}
void Client::run() noexcept {
    std::unique_ptr<Connection> connection;
    auto last_activity = Clock::now();
    while (!quitting) {
        Command job;
        {
            std::unique_lock lock(mutex);
            changed.wait_for(lock, std::chrono::seconds(1), [&] { return command.has_value() || quitting; });
            if (quitting)
                break;
            if (!command) {
                if (connection && Clock::now() - last_activity >= heartbeat_interval)
                    job.operation = "heartbeat";
                else
                    continue;
            } else {
                job = std::move(*command);
                command.reset();
            }
        }
        try {
            if (job.operation == "select") {
                bool same = false;
                {
                    std::lock_guard lock(mutex);
                    same = connection && state.value("favoriteId", uint64_t{}) == job.favorite;
                }
                if (!same) {
                    connection.reset();
                    load_pairing(job);
                    std::lock_guard lock(mutex);
                    if (state.value("passwordSaved", false)) {
                        job.operation = "connect";
                        job.remember = true;
                        job.endpoint = state["endpoint"].get<std::string>();
                        job.pin = state["fingerprint"].get<std::string>();
                    }
                }
            }
            if (job.operation == "select") {
                // Re-entering the same server keeps the existing authenticated connection.
            } else if (job.operation == "connect") {
                connection.reset();
                {
                    std::lock_guard lock(mutex);
                    state = {{"state", "connecting"},
                             {"pending", true},
                             {"favoriteId", job.favorite},
                             {"name", job.name},
                             {"endpoint", job.endpoint},
                             {"fingerprint", job.pin},
                             {"passwordSaved", passwords.contains(job.game_endpoint, job.endpoint, job.pin)},
                             {"message", "Verifying the server and authenticating..."}};
                    publish();
                }
                selected_game = job.game_endpoint;
                if (job.password.empty())
                    job.password = passwords.get(job.game_endpoint, job.endpoint, job.pin);
                std::string retained = job.remember ? job.password : std::string{};
                struct WipeRetained {
                    std::string &s;
                    ~WipeRetained() { erase(s); }
                } wipe_retained{retained};
                auto next = std::make_unique<Connection>();
                next->connect(job.endpoint, job.pin, job.password, cancelled);
                connection = std::move(next);
                refresh(*connection);
                try {
                    save_pairing(job);
                    if (job.remember)
                        passwords.put(job.game_endpoint, job.endpoint, job.pin, retained);
                    else
                        passwords.forget(job.game_endpoint);
                    std::lock_guard lock(mutex);
                    state["passwordSaved"] = job.remember;
                    publish();
                } catch (const std::exception &e) {
                    std::lock_guard lock(mutex);
                    state["message"] = std::string("Connected. Identity not saved: ") + e.what();
                    publish();
                }
            } else if (job.operation == "forget") {
                passwords.forget(selected_game);
                std::lock_guard lock(mutex);
                state["passwordSaved"] = false;
                state["message"] = "Password forgotten on this client.";
                publish();
            } else if (job.operation == "disconnect") {
                connection.reset();
                std::lock_guard lock(mutex);
                state["state"] = "idle";
                state["message"] = "Disconnected.";
                state.erase("configs");
                state.erase("mods");
                state.erase("server");
                state.erase("translations");
                publish();
            } else {
                if (!connection)
                    throw Error("disconnected", "Reconnect to the server.");
                if (job.operation == "heartbeat") {
                    (void)connection->request("hello");
                    last_activity = Clock::now();
                    continue;
                }
                if (job.operation == "refresh")
                    refresh(*connection);
                if (job.operation.starts_with("server.") || job.operation.starts_with("mods.") ||
                    job.operation.starts_with("balance.")) {
                    auto response = connection->request(job.operation, job.payload);
                    validate_extra(job.operation, response);
                    std::lock_guard lock(mutex);
                    state[section_for(job.operation)] = std::move(response);
                    if (job.operation == "server.restart") {
                        connection.reset();
                        state["state"] = "idle";
                        state["message"] = "Restart requested. Wait, then reconnect.";
                    } else
                        state["message"] = job.operation.ends_with(".write")
                                               ? "Saved. Changes will apply at the next restart."
                                               : "Data refreshed.";
                    publish();
                }
                if (job.operation == "save") {
                    job.payload["writeId"] = hex(random_bytes(16));
                    auto saved = connection->request("mod.config.write", job.payload);
                    std::lock_guard lock(mutex);
                    const auto id = job.payload.at("modId").get<std::string>();
                    auto next = state.at("configs");
                    next[id] = saved;
                    validate_remote(state.at("server"), state.at("mods"), next);
                    state["configs"] = std::move(next);
                    state["lastWriteId"] = job.payload.at("writeId");
                    state["message"] = "Settings saved. Restart the server to apply them.";
                    publish();
                }
            }
            last_activity = Clock::now();
        } catch (const Error &e) {
            bool keep = e.code == "conflict" || e.code == "invalid_values" || e.code == "mod_not_editable" ||
                        e.code == "write_failed" || e.code == "unavailable" || e.code == "file_unavailable";
            if (!keep)
                connection.reset();
            std::lock_guard lock(mutex);
            state["message"] = e.what();
            state["errorCode"] = e.code;
            if (!connection) {
                state["state"] = "error";
                state.erase("configs");
                state.erase("mods");
                state.erase("server");
                state.erase("translations");
            }
            publish();
        } catch (const std::exception &e) {
            connection.reset();
            std::lock_guard lock(mutex);
            state["state"] = "error";
            state["message"] = e.what();
            state.erase("configs");
            state.erase("mods");
            state.erase("server");
            state.erase("translations");
            publish();
        } catch (...) {
            connection.reset();
            std::lock_guard lock(mutex);
            state["state"] = "error";
            state["message"] = "Internal administration error.";
            publish();
        }
        {
            std::lock_guard lock(mutex);
            state["pending"] = command.has_value();
            publish();
        }
    }
    connection.reset();
    std::lock_guard lock(mutex);
    command.reset();
    state = {{"state", "idle"}, {"pending", false}, {"favoriteId", 0}, {"message", ""}};
    publish();
}
} // namespace bc::admin
