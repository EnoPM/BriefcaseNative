#include "Service.hpp"
#include "../Briefcase.Localization/Catalog.hpp"
#include "../Briefcase.NativeHost/Configuration.hpp"
#include "../Briefcase.NativeHost/Startup.hpp"
#ifdef _WIN32
#include <Windows.h>
#include <sddl.h>
#endif
#include <fstream>
#include <set>
namespace bc::admin {
static void need(bool condition, const char *code, const char *message) {
    if (!condition)
        throw Error(code, message);
}
static bool token(const std::string &s, size_t length) {
    return s.size() == length && std::all_of(s.begin(), s.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
static void fields(const Json &j, std::initializer_list<const char *> names) {
    need(j.is_object() && j.size() == names.size(), "invalid_request", "Invalid request fields.");
    for (auto name : names)
        need(j.contains(name), "invalid_request", "Missing request field.");
}
#ifdef _WIN32
std::string read_file(const fs::path &path, size_t limit) {
    assert_plain_path(path);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    need(file != INVALID_HANDLE_VALUE, "file_unavailable", "File unavailable.");
    struct Close {
        HANDLE h;
        ~Close() { CloseHandle(h); }
    } close{file};
    BY_HANDLE_FILE_INFORMATION info{};
    need(GetFileInformationByHandle(file, &info) &&
             !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
             info.nNumberOfLinks == 1,
         "unsafe_path", "Redirected file refused.");
    LARGE_INTEGER size{};
    need(GetFileSizeEx(file, &size) && size.QuadPart >= 0 && uint64_t(size.QuadPart) <= limit,
         "file_too_large", "File is too large.");
    std::string out(size_t(size.QuadPart), 0);
    DWORD count{};
    need(ReadFile(file, out.data(), DWORD(out.size()), &count, nullptr) && count == out.size(),
         "file_unavailable", "Incomplete read.");
    return out;
}
void write_file(const fs::path &path, std::string_view text, bool replace, bool private_file) {
    assert_plain_path(path.parent_path());
    if (fs::exists(path)) {
        need(replace, "already_configured", "The file already exists.");
        (void)read_file(path, 1048576); // Also rejects links before replacement.
    }
    const auto temporary =
        path.parent_path() / (path.filename().wstring() + L"." + std::to_wstring(GetCurrentProcessId()) +
                              L"." + fs::path(hex(random_bytes(8))).wstring() + L".tmp");
    PSECURITY_DESCRIPTOR descriptor{};
    if (private_file)
        need(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;SY)(A;;FA;;;OW)",
                                                                  SDDL_REVISION_1, &descriptor, nullptr),
             "permissions", "Private file permissions unavailable.");
    struct Free {
        PSECURITY_DESCRIPTOR d;
        ~Free() {
            if (d)
                LocalFree(d);
        }
    } free{descriptor};
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, private_file ? &attributes : nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    need(file != INVALID_HANDLE_VALUE, "write_failed", "Unable to create temporary file.");
    bool ok = false;
    DWORD written{};
    ok = WriteFile(file, text.data(), DWORD(text.size()), &written, nullptr) && written == text.size() &&
         FlushFileBuffers(file);
    CloseHandle(file);
    if (ok) {
        try {
            assert_plain_path(path.parent_path());
            if (fs::exists(path))
                (void)read_file(path, 1048576);
        } catch (...) {
            DeleteFileW(temporary.c_str());
            throw;
        }
        ok = MoveFileExW(temporary.c_str(), path.c_str(),
                         MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0));
    }
    if (!ok)
        DeleteFileW(temporary.c_str());
    need(ok, "write_failed", "Atomic save failed. Previous values were preserved.");
}
#endif
Json Settings::serialize() const {
    return {{"version", 1},
            {"serverId", server_id},
            {"listen", listen_address},
            {"port", port},
            {"endpoint", endpoint},
            {"certificate", identity.certificate},
            {"protectedKey", identity.protected_key},
            {"password", {{"salt", hex(salt)}, {"verifier", hex(verifier)}, {"iterations", iterations}}}};
}
Settings Settings::parse(const Json &j) {
    fields(j,
           {"version", "serverId", "listen", "port", "endpoint", "certificate", "protectedKey", "password"});
    need((j.at("version") == 1 || j.at("version") == 2) && j.at("port").is_number_integer(),
         "invalid_settings", "Incompatible administration configuration.");
    const int port = j.at("port").get<int>();
    need(port > 0 && port <= 65535, "invalid_settings", "Invalid administration port.");
    Settings out;
    out.port = uint16_t(port);
    out.server_id = j.at("serverId").get<std::string>();
    need(token(out.server_id, 32), "invalid_settings", "Invalid server identity.");
    out.listen_address = j.at("listen").get<std::string>();
    out.endpoint = j.at("endpoint").get<std::string>();
    need(out.listen_address.size() <= 64 && out.endpoint.size() <= 255, "invalid_settings",
         "Invalid administration address.");
    out.identity.certificate = j.at("certificate").get<std::string>();
    out.identity.protected_key = j.at("protectedKey").get<std::string>();
    auto der = unhex(out.identity.certificate);
    out.identity.fingerprint = digest({reinterpret_cast<char *>(der.data()), der.size()});
    const auto &password = j.at("password");
    if (j.at("version") == 2) {
        need(password.is_string(), "invalid_settings", "Administration password must be a string.");
        const auto &plain = password.get_ref<const std::string &>();
        need(plain.size() >= 12 && plain.size() <= 256 && plain.find('\0') == plain.npos, "password_policy",
             "Administration password must contain 12 to 256 UTF-8 bytes without NUL.");
        // Preserve the existing authentication protocol; cleartext is not retained by the running service.
        out.salt = random_bytes(32);
        out.verifier = derive(plain, out.salt, out.iterations);
        return out;
    }
    fields(password, {"salt", "verifier", "iterations"});
    out.salt = unhex(password.at("salt").get<std::string>());
    out.verifier = unhex(password.at("verifier").get<std::string>());
    need(password.at("iterations").is_number_unsigned() || password.at("iterations").is_number_integer(),
         "invalid_settings", "Invalid password work factor.");
    out.iterations = password.at("iterations").get<uint64_t>();
    need(out.salt.size() == 32 && out.verifier.size() == 32 && out.iterations >= 600000 &&
             out.iterations <= 2000000,
         "invalid_settings", "Invalid password protection.");
    return out;
}
Settings Settings::load(const fs::path &path) {
    auto text = read_file(path);
#ifndef _WIN32
    const auto permissions = fs::status(path).permissions();
    need((permissions & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none,
         "permissions", "Administration configuration must be private (chmod 600).");
#endif
    Json document;
    struct Wipe {
        std::string &text;
        Json &document;
        ~Wipe() {
            erase(text);
            if (document.is_object() && document.contains("password") && document["password"].is_string())
                erase(document["password"].get_ref<std::string &>());
        }
    } wipe{text, document};
    try {
        document = bc::strict_json(text);
        return parse(document);
    } catch (const Error &) {
        throw;
    } catch (...) {
        // JSON parse errors may quote input bytes. Never forward a configured secret to the log.
        throw Error("invalid_settings", "Invalid administration configuration. Check the local file.");
    }
}
Settings provision(const fs::path &root, const std::string &address, uint16_t port,
                   const std::string &endpoint, std::string_view password) {
    need(password.size() >= 12 && password.size() <= 256 && password.find('\0') == password.npos,
         "password_policy", "Choose a password between 12 and 256 bytes.");
    need(port > 0, "invalid_settings", "Invalid administration port.");
    assert_plain_path(root);
    const auto directory = root / "Admin";
    if (!fs::exists(directory))
        fs::create_directory(directory);
    assert_plain_path(directory);
    need(!fs::exists(directory / "server.json") && !fs::exists(directory / "pairing.json"),
         "already_configured", "Administration is already configured. Preserve the existing identity.");
    Settings settings;
    settings.server_id = hex(random_bytes(16));
    settings.listen_address = address;
    settings.port = port;
    settings.endpoint = endpoint;
    settings.identity = create_identity();
    settings.salt = random_bytes(32);
    settings.verifier = derive(password, settings.salt, settings.iterations);
    // Version 2 intentionally stores the administrator password in the private local configuration.
    // TLS identity protection and the public pairing remain unchanged.
    auto document = settings.serialize();
    document["version"] = 2;
    document["password"] = password;
    std::string text;
    struct WipePassword {
        Json &document;
        std::string &text;
        ~WipePassword() {
            erase(document["password"].get_ref<std::string &>());
            erase(text);
        }
    } wipePassword{document, text};
    (void)Settings::parse(document);
    text = document.dump(2) + "\n";
    write_file(directory / "server.json", text, false, true);
    Json pairing{{"version", 1},
                 {"serverId", settings.server_id},
                 {"endpoint", endpoint},
                 {"fingerprint", settings.identity.fingerprint}};
    write_file(directory / "pairing.json", pairing.dump(2) + "\n", false);
    return settings;
}
bool ConfigStore::allowed(std::string_view id) {
    if (id.empty() || id.size() > 128) return false;
    bool separator = true;
    for (char c : id) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) separator = false;
        else if ((c == '.' || c == '-') && !separator) separator = true;
        else return false;
    }
    return !separator;
}
void ConfigStore::add(const std::string &id, fs::path path, const Json &schema, const Json &active) {
    need(allowed(id), "mod_not_editable", "This mod has no administration settings.");
    const auto normalized = normalize_config(schema, active);
    assert_plain_path(path);
    std::lock_guard lock(mutex);
    need(!entries.contains(id), "duplicate_mod", "Mod already registered.");
    entries.emplace(id, Entry{id, std::move(path), schema, normalized});
}
bool ConfigStore::contains(const std::string &id) {
    std::lock_guard lock(mutex);
    return entries.contains(id);
}
Json ConfigStore::snapshot(const Entry &e) {
    auto saved = normalize_config(e.schema, strict_json(read_file(e.path)));
    auto metadata = locale::presentation(e.path.parent_path() / "presentation.json");
    auto schema = locale::apply_presentation(e.schema, metadata.value("settings", Json::object()),
                                             metadata.value("categories", Json::object()), "mods." + e.id);
    return {{"schema", schema},
            {"active", e.active},
            {"saved", saved},
            {"revision", digest(saved.dump())},
            {"restartRequired", saved != e.active}};
}
Json ConfigStore::read(const std::string &id) {
    std::lock_guard lock(mutex);
    auto it = entries.find(id);
    need(it != entries.end(), "mod_not_editable", "Settings unavailable for this mod.");
    auto out = snapshot(it->second);
    out["modId"] = id;
    return out;
}
Json ConfigStore::write(const Json &p) {
    fields(p, {"modId", "expectedRevision", "writeId", "values"});
    auto id = p.at("modId").get<std::string>(), revision = p.at("expectedRevision").get<std::string>(),
         write_id = p.at("writeId").get<std::string>();
    need(token(revision, 64) && token(write_id, 32), "invalid_request",
         "Invalid revision or write identifier.");
    const auto hash = digest(p.dump());
    std::lock_guard lock(mutex);
    if (auto found = receipts.find(write_id); found != receipts.end()) {
        need(found->second.request_hash == hash, "write_id_reused",
             "Write identifier already used for another change.");
        return found->second.response;
    }
    auto it = entries.find(id);
    need(it != entries.end(), "mod_not_editable", "This mod cannot be edited.");
    auto &entry = it->second;
    auto current = snapshot(entry);
    need(current.at("revision") == revision, "conflict", "Settings changed. Refresh before saving.");
    need(p.at("values").is_object() && p.at("values").size() == entry.schema.at("properties").size(),
         "invalid_values", "All settings must be provided.");
    Json values;
    try {
        values = normalize_config(entry.schema, p.at("values"));
    } catch (const std::exception &e) {
        throw Error("invalid_values", e.what());
    }
    if (values != current.at("saved"))
        write_file(entry.path, values.dump(2) + "\n");
    auto response = snapshot(entry);
    response["modId"] = id;
    if (receipts.size() >= 128) {
        receipts.erase(receipt_order.front());
        receipt_order.pop_front();
    }
    receipts.emplace(write_id, Receipt{hash, response});
    receipt_order.push_back(write_id);
    return response;
}
Server::Server(Settings settings, std::shared_ptr<ConfigStore> configs, std::function<Json()> status,
               std::function<Json()> mods, std::function<void(std::string_view)> log,
               std::function<Json(const std::string &, const Json &)> extension,
               std::function<void()> restarted)
    : settings(std::move(settings)), configs(std::move(configs)), status(std::move(status)),
      mods(std::move(mods)), log(std::move(log)), extension(std::move(extension)),
      restarted(std::move(restarted)) {}
Server::~Server() {
    stop();
}
void Server::start() {
    need(workers.empty(), "already_started", "Service already started.");
    stopping = false;
    credentials = std::make_unique<Credentials>(settings.identity);
    listener = std::make_unique<Listener>(settings.listen_address, settings.port, stopping);
    try {
        for (int i = 0; i < 4; ++i)
            workers.emplace_back([this] { worker(); });
    } catch (...) {
        stop();
        throw;
    }
    if (log)
        log("Administration: TLS listener ready on configured endpoint; 4 bounded workers.");
}
void Server::request_stop() noexcept {
    stopping = true;
}
void Server::stop() noexcept {
    request_stop();
    for (auto &worker : workers)
        if (worker.joinable())
            worker.join();
    workers.clear();
    listener.reset();
    credentials.reset();
}
uint16_t Server::port() const {
    return listener ? listener->port() : 0;
}
bool Server::admit(const std::string &peer) {
    std::lock_guard lock(attempts_mutex);
    const auto before = Clock::now() - std::chrono::minutes(1);
    auto clean = [&](auto &q) {
        while (!q.empty() && q.front() < before)
            q.pop_front();
    };
    clean(global_attempts);
    for (auto it = attempts.begin(); it != attempts.end();) {
        clean(it->second);
        if (it->second.empty())
            it = attempts.erase(it);
        else
            ++it;
    }
    if (global_attempts.size() >= 12)
        return false;
    auto &local = attempts[peer];
    if (local.size() >= 4)
        return false;
    local.push_back(Clock::now());
    global_attempts.push_back(Clock::now());
    return true;
}
static Json response(const Json &id, bool ok, Json payload) {
    Json out{{"version", 1}, {"requestId", id}, {"ok", ok}};
    out[ok ? "payload" : "error"] = std::move(payload);
    return out;
}
void Server::serve(Stream &stream) {
    bool authenticated = false;
    const auto created = Clock::now();
    unsigned count = 0;
    auto period = created;
    for (;;) {
        auto raw = stream.receive(65536, authenticated ? 120 : 10);
        struct Wipe {
            std::string &s;
            ~Wipe() { erase(s); }
        } wipe{raw};
        Json request;
        try {
            request = strict_json(raw);
        } catch (...) {
            throw Error("invalid_request", "Invalid JSON.");
        }
        Json id = nullptr;
        std::string operation;
        bool close = false;
        Json reply;
        try {
            fields(request, {"version", "requestId", "operation", "payload"});
            id = request.at("requestId");
            need(id.is_string() && token(id.get<std::string>(), 32), "invalid_request",
                 "Invalid identifier.");
            need(request.at("version").is_number_integer() && request.at("version") == 1, "version_mismatch",
                 "Incompatible protocol version.");
            operation = request.at("operation").get<std::string>();
            need(operation.size() <= 64 && request.at("payload").is_object(), "invalid_request",
                 "Invalid operation.");
            if (Clock::now() - period >= std::chrono::seconds(1)) {
                period = Clock::now();
                count = 0;
            }
            need(++count <= 30, "rate_limited", "Too many commands. Wait before retrying.");
            need(authenticated || Clock::now() - created < std::chrono::seconds(10), "session_expired",
                 "Authentication timed out.");
            auto &payload = request.at("payload");
            Json result;
            if (operation == "hello") {
                fields(payload, {});
                result = {{"serverId", settings.server_id}, {"protocol", 1}};
            } else if (operation == "authenticate") {
                close = true;
                need(!authenticated, "already_authenticated", "Session already authenticated.");
                fields(payload, {"password"});
                need(payload.at("password").is_string(), "unauthorized", "Authentication refused.");
                auto &password = payload.at("password").get_ref<std::string &>();
                struct WipePassword {
                    std::string &s;
                    ~WipePassword() { erase(s); }
                } wipe_password{password};
                need(password.size() >= 12 && password.size() <= 256, "unauthorized",
                     "Authentication refused.");
                need(admit(stream.peer()), "rate_limited", "Too many attempts. Try again in a minute.");
                auto key = derive(password, settings.salt, settings.iterations);
                const bool valid = equal(key, settings.verifier);
                erase(key);
                need(valid, "unauthorized", "Incorrect password.");
                authenticated = true;
                close = false;
                result = {{"authenticated", true}, {"expiresInSeconds", 0}};
                if (log)
                    log("Administration: authenticated session established.");
            } else {
                need(authenticated, "unauthorized", "Authentication required.");
                if (operation == "server.status") {
                    fields(payload, {});
                    result = status();
                } else if (operation == "mods.list") {
                    fields(payload, {});
                    result = mods();
                } else if (operation == "mod.config.read") {
                    fields(payload, {"modId"});
                    result = configs->read(payload.at("modId").get<std::string>());
                } else if (operation == "mod.config.write") {
                    result = configs->write(payload);
                    if (log)
                        log("Administration: saved settings for " + payload.at("modId").get<std::string>() +
                            "; restart required if values changed.");
                } else if (operation == "logout") {
                    fields(payload, {});
                    close = true;
                    result = {{"loggedOut", true}};
                } else if (extension)
                    result = extension(operation, payload);
                else
                    throw Error("unknown_operation", "Command unavailable.");
            }
            reply = response(id, true, std::move(result));
        } catch (const Error &e) {
            reply = response(id, false, {{"code", e.code}, {"message", e.what()}});
            if (e.code == "session_expired" || e.code == "rate_limited")
                close = true;
        } catch (const std::exception &) {
            reply = response(
                id, false,
                {{"code", "invalid_request"}, {"message", "Invalid request or unavailable resource."}});
        }
        // Explicitly wipe the password even when field validation rejected the request.
        if (request.is_object() && request.contains("payload") && request["payload"].is_object() &&
            request["payload"].contains("password") && request["payload"]["password"].is_string())
            erase(request["payload"]["password"].get_ref<std::string &>());
        stream.send(reply.dump());
        if (operation == "server.restart" && reply.value("ok", false) && restarted)
            restarted();
        if (close)
            return;
    }
}
void Server::worker() noexcept {
    while (!stopping) {
        try {
            if (auto stream = listener->accept(*credentials))
                serve(*stream);
            else
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
        } catch (...) { // Untrusted connections cannot unwind a worker or flood the game log.
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
}
void Connection::connect(const std::string &endpoint, const std::string &pin, std::string &password,
                         const std::atomic<bool> &stop) {
    struct Wipe {
        std::string &s;
        ~Wipe() { erase(s); }
    } wipe{password};
    stream = std::make_unique<Stream>(Stream::connect(credentials, endpoint, pin, stop));
    protocol = stream->protocol();
    auto hello = request("hello");
    server_id = hello.at("serverId").get<std::string>();
    Json auth{{"password", password}};
    struct WipeJson {
        Json &j;
        ~WipeJson() { erase(j["password"].get_ref<std::string &>()); }
    } wipe_json{auth};
    (void)request("authenticate", auth);
}
Json Connection::request(const std::string &operation, const Json &payload) {
    need(bool(stream), "disconnected", "Session is not connected.");
    const auto id = hex(random_bytes(16));
    Json envelope{{"version", 1}, {"requestId", id}, {"operation", operation}, {"payload", payload}};
    struct WipeEnvelope {
        Json &j;
        ~WipeEnvelope() {
            auto &p = j["payload"];
            if (p.is_object() && p.contains("password") && p["password"].is_string())
                erase(p["password"].get_ref<std::string &>());
        }
    } wipe_envelope{envelope};
    std::string wire = envelope.dump();
    struct Wipe {
        std::string &s;
        ~Wipe() { erase(s); }
    } wipe{wire};
    stream->send(wire, 65536);
    auto result = strict_json(stream->receive(1048576, 10), 1048576);
    need(result.is_object() && result.size() == 4 && result.at("version") == 1 &&
             result.at("requestId") == id && result.at("ok").is_boolean(),
         "protocol_error", "Incompatible server response.");
    if (!result.at("ok").get<bool>()) {
        const auto &error = result.at("error");
        auto code = error.at("code").get<std::string>(), message = error.at("message").get<std::string>();
        need(code.size() <= 64 && message.size() <= 512, "protocol_error", "Invalid remote error.");
        throw Error(code, message);
    }
    return result.at("payload");
}
} // namespace bc::admin
