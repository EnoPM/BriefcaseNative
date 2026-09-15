// Services have process lifetime, but all workers are stopped outside the game/render threads.
static admin::Client *client_admin{};
static BcResult BC_CALL admin_select(uint64_t id) noexcept {
    try {
        if (!client_admin)
            return BC_NOT_READY;
        auto entry = server_directory.find(id);
        if (!entry)
            return BC_NOT_FOUND;
        return client_admin->select(id, entry->name, entry->endpoint) ? BC_OK : BC_NOT_READY;
    } catch (...) {
        return BC_INTERNAL;
    }
}
static BcResult BC_CALL admin_connect(uint64_t id, const char *endpoint, const char *pin,
                                      const char *password, uint32_t remember) noexcept {
    try {
        if (!client_admin)
            return BC_NOT_READY;
        if (!endpoint || !pin || !password || strnlen_s(endpoint, 256) >= 256 || strnlen_s(pin, 65) != 64 ||
            strnlen_s(password, 257) > 256)
            return BC_INVALID_ARGUMENT;
        auto entry = server_directory.find(id);
        if (!entry)
            return BC_NOT_FOUND;
        return client_admin->connect(id, entry->name, entry->endpoint, endpoint, pin, password, remember != 0)
                   ? BC_OK
                   : BC_INVALID_ARGUMENT;
    } catch (...) {
        return BC_INVALID_ARGUMENT;
    }
}
static BcResult BC_CALL admin_command(const char *operation, const char *payload) noexcept {
    try {
        if (!client_admin)
            return BC_NOT_READY;
        if (!operation || !payload || strnlen_s(operation, 64) >= 64 || strnlen_s(payload, 65537) > 65536)
            return BC_INVALID_ARGUMENT;
        return client_admin->submit(operation, strict_json(payload)) ? BC_OK : BC_NOT_READY;
    } catch (...) {
        return BC_INVALID_ARGUMENT;
    }
}
static void BC_CALL admin_disconnect() noexcept {
    try {
        if (client_admin)
            client_admin->disconnect();
    } catch (...) {
    }
}
static BcResult BC_CALL admin_snapshot(char *buffer, uint32_t capacity, uint64_t *version) noexcept {
    try {
        if (!client_admin)
            return BC_NOT_READY;
        if (!buffer || capacity < 2 || !version)
            return BC_INVALID_ARGUMENT;
        std::string text;
        auto next = *version;
        if (!client_admin->snapshot(text, next)) {
            buffer[0] = 0;
            return BC_OK;
        }
        if (text.size() >= capacity)
            return BC_LIMIT;
        memcpy(buffer, text.c_str(), text.size() + 1);
        *version = next;
        return BC_OK;
    } catch (...) {
        return BC_INTERNAL;
    }
}
#include "ServerAdmin.inc"
static void request_admin_stop() noexcept {
    if (client_admin)
        client_admin->request_stop();
    if (server_admin)
        server_admin->request_stop();
}
static void stop_admin_workers() noexcept {
    if (client_admin)
        client_admin->stop();
    if (server_admin)
        server_admin->stop();
}
