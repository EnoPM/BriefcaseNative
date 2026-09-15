// Private framework services; no disk I/O or Unreal invocation on the render thread.
static servers::Directory server_directory;
static std::atomic<uint32_t> server_join_state{};
static std::mutex server_join_mutex;
static std::string server_join_message;
static BcResult BC_CALL client_servers(BcClientServerList *out) noexcept {
    if (!out || out->size < sizeof(*out))
        return BC_INVALID_ARGUMENT;
    try {
        auto snapshot = server_directory.snapshot();
        *out = {};
        out->size = sizeof(*out);
        out->count = uint32_t(snapshot.entries.size());
        out->pending = snapshot.pending;
        out->writable = snapshot.writable;
        text_field(out->message, snapshot.message);
        {
            std::lock_guard lock(server_join_mutex);
            out->join_state = server_join_state.load();
            text_field(out->join_message, server_join_message);
        }
        for (size_t i = 0; i < snapshot.entries.size(); ++i) {
            const auto &e = snapshot.entries[i];
            out->entries[i].id = e.id;
            text_field(out->entries[i].name, e.name);
            text_field(out->entries[i].endpoint, e.endpoint);
        }
        return BC_OK;
    } catch (...) {
        return BC_INTERNAL;
    }
}
static BcResult BC_CALL client_add_server(const char *name, const char *endpoint) noexcept {
    if (!name || !endpoint || strnlen_s(name, 96) == 96 || strnlen_s(endpoint, 256) == 256)
        return BC_INVALID_ARGUMENT;
    try {
        return server_directory.add(name, endpoint);
    } catch (...) {
        return BC_INTERNAL;
    }
}
static BcResult BC_CALL client_remove_server(uint64_t id) noexcept {
    try {
        return server_directory.remove(id);
    } catch (...) {
        return BC_INTERNAL;
    }
}
static void server_join_result(uint32_t state, std::string message) {
    std::lock_guard lock(server_join_mutex);
    server_join_message = std::move(message);
    server_join_state = state;
}
static void BC_CALL connect_saved_server(void *context) noexcept {
    std::unique_ptr<servers::Entry> entry(static_cast<servers::Entry *>(context));
    try {
        std::string error;
        const auto result = backend_client_connect(entry->endpoint, error);
        if (result == BC_OK)
            server_join_result(2, "Connection requested to " + entry->name + ".");
        else {
            if (error.empty())
                error = "Connection unavailable (code " + std::to_string(result) + ").";
            log("Client server join failed: " + error);
            server_join_result(3, std::move(error));
        }
    } catch (...) {
        log("Client server join failed: unexpected exception");
        try {
            server_join_result(3, "Error connecting to the server.");
        } catch (...) {
        }
    }
}
static BcResult BC_CALL client_join_server(uint64_t id) noexcept {
    try {
        if (environment != Environment::client || !backend_ready())
            return BC_NOT_READY;
        auto entry = server_directory.find(id);
        if (!entry)
            return BC_NOT_FOUND;
        auto data = std::make_unique<servers::Entry>(std::move(*entry));
        {
            std::lock_guard lock(server_join_mutex);
            if (server_join_state == 1)
                return BC_NOT_READY;
            server_join_message = "Connecting...";
            server_join_state = 1;
        }
        const auto result = backend_post(connect_saved_server, data.get());
        if (result == BC_OK)
            data.release();
        else
            server_join_result(3, "Unable to schedule the connection.");
        return result;
    } catch (...) {
        try {
            server_join_result(3, "Connection error.");
        } catch (...) {
        }
        return BC_INTERNAL;
    }
}
