#include "Client.hpp"
#include "../Briefcase.Admin/Service.hpp"
#include "../Briefcase.Client.Input/Keys.hpp"
#include "../Briefcase.NativeHost/Configuration.hpp"
#include "../Briefcase.NativeHost/Startup.hpp"
namespace bc::locale {
Client::Client(Path root, std::vector<Manifest> mods) : root(std::move(root)), mods(std::move(mods)) {
    // Tiny preference file only, on the host worker. Translation/font loading remains lazy.
    try {
        auto file = this->root / "ui-settings.json";
        if (std::filesystem::exists(file)) {
            auto prefs = bc::strict_json(admin::read_file(file));
            if (prefs.contains("menuKey") && prefs["menuKey"].is_number_unsigned()) {
                auto key = prefs["menuKey"].get<uint64_t>();
                if (key < 256 && input::valid_menu_key(uint32_t(key)))
                    menu_key_ = uint32_t(key);
            }
        }
    } catch (...) {
    }
    state["menuKey"] = menu_key();
    serialized = state.dump();
}
Client::~Client() {
    stop();
}
void Client::stop() {
    {
        std::lock_guard lock(mutex);
        stopping = true;
        wake.notify_all();
    }
    if (worker.joinable())
        worker.join();
}
bool Client::snapshot(std::string &out, uint64_t &version) {
    std::lock_guard lock(mutex);
    if (!started) {
        started = true;
        requested = true;
        state["pending"] = true;
        serialized = state.dump();
        ++sequence;
        worker = std::thread([this] { run(); });
        wake.notify_all();
    }
    if (version == sequence)
        return false;
    out = serialized;
    version = sequence;
    return true;
}
bool Client::select(const std::string &language) {
    std::lock_guard lock(mutex);
    if (!valid_language(language) || !state["languages"].contains(language) || requested || busy ||
        stopping || !started)
        return false;
    requested_language = language;
    requested = true;
    state["pending"] = true;
    serialized = state.dump();
    ++sequence;
    wake.notify_one();
    return true;
}
bool Client::select_menu_key(uint32_t key) {
    if (!input::valid_menu_key(key))
        return false;
    std::lock_guard lock(mutex);
    if (stopping || requested || busy || !started)
        return false;
    requested_key = key;
    requested = true;
    state["pending"] = true;
    state.erase("errorKey");
    serialized = state.dump();
    ++sequence;
    wake.notify_one();
    return true;
}
void Client::run() {
    for (;;) {
        std::string selected;
        uint32_t new_key{};
        {
            std::unique_lock lock(mutex);
            wake.wait(lock, [&] { return stopping || requested; });
            if (stopping)
                return;
            selected = requested_language;
            requested = false;
            busy = true;
            new_key = requested_key;
            requested_key = 0;
            requested_language.clear();
        }
        try {
            auto local = read_languages(root / "Localization");
            auto overrides = read_languages(root / "Localization" / "Overrides");
            for (auto it = overrides.begin(); it != overrides.end(); ++it)
                local[it.key()].update(it.value());
            if (local.empty())
                throw std::runtime_error("No framework languages installed");
            Json languages = Json::object();
            for (auto it = local.begin(); it != local.end(); ++it)
                languages[it.key()] = it.value().value("language.name", it.key());
            for (auto &mod : mods)
                if (mod.environment != "server" && !mod.directory.empty()) {
                    auto bundle = read_languages(mod.directory / "Translations", "mods." + mod.id + ".");
                    for (auto it = bundle.begin(); it != bundle.end(); ++it)
                        local[it.key()].update(it.value());
                }
            auto file = root / "ui-settings.json";
            auto preferences = Json::object();
            if (std::filesystem::exists(file))
                preferences = bc::strict_json(admin::read_file(file));
            if (selected.empty())
                selected = preferences.value("language", "fr");
            if (!languages.contains(selected))
                selected = languages.contains("en") ? "en" : languages.begin().key();
            validate_catalogues(local, false);
            const auto key = new_key ? new_key : menu_key();
            if (preferences.value("language", std::string{}) != selected ||
                !preferences.contains("menuKey") || preferences["menuKey"] != key) {
                preferences["language"] = selected;
                preferences["menuKey"] = key;
                admin::write_file(file, preferences.dump(2) + "\n");
            }
            std::lock_guard lock(mutex);
            menu_key_ = key;
            busy = false;
            state = {{"language", selected},
                     {"languages", languages},
                     {"catalogues", local},
                     {"pending", false},
                     {"menuKey", key}};
            serialized = state.dump();
            ++sequence;
        } catch (...) {
            std::lock_guard lock(mutex);
            state["pending"] = false;
            busy = false;
            state["errorKey"] = new_key ? "ui.keybind.save_failed" : "ui.language_load_failed";
            serialized = state.dump();
            ++sequence;
        }
    }
}
} // namespace bc::locale
