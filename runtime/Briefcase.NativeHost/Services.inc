// Private implementation included in Host.cpp, after its capability/context helpers.
static uint8_t *game_image;
static fs::path server_config_root;
static bool startup_phase = true;
static Scope *loading_scope{};
static std::atomic<DWORD> loading_thread{};
static BcResult copy_string(std::string_view text, char *out, uint32_t capacity, uint32_t *required) {
    if (!required || text.size() >= 65536)
        return BC_INVALID_ARGUMENT;
    *required = static_cast<uint32_t>(text.size() + 1);
    if (capacity < *required || !out)
        return BC_LIMIT;
    std::memcpy(out, text.data(), text.size());
    out[text.size()] = 0;
    return BC_OK;
}
static BcResult BC_CALL config_load(void *c, const char *schema, uint32_t length, char *out,
                                    uint32_t capacity, uint32_t *required) noexcept {
    try {
        auto *s = scope(c, BC_CAP_CONFIG);
        if (GetCurrentThreadId() != loading_thread || !s || loading_scope != s)
            return BC_DENIED;
        if (!schema || !length || length > 65536)
            return BC_INVALID_ARGUMENT;
        const std::string schemaText(schema, length);
        if (s->config_schema.empty()) {
            const auto document = strict_json(schemaText);
            const auto data = s->manifest.directory / "Data";
            assert_plain_path(data);
            fs::create_directories(data);
            const auto path = data / "config.json";
            const auto input =
                fs::exists(path) ? strict_json(read_bounded(path, 65536)) : nlohmann::json::object();
            auto config = normalize_config(document, input);
            s->config_text = config.dump(2) + "\n";
            if (s->config_text.size() >= 65536)
                throw std::runtime_error("Normalized configuration exceeds 64 KiB");
            auto save = [](const fs::path &path, const std::string &text) {
                assert_plain_path(path);
                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                out << text;
                out.flush();
                if (!out)
                    throw std::runtime_error("Cannot save configuration/schema");
            };
            if (!fs::exists(path))
                save(path, s->config_text);
            save(data / "config.schema.json", document.dump(2) + "\n");
            s->config_schema = schemaText;
            if(environment==Environment::client) s->client_settings.initialize(path,document,config);
            log(s->manifest.id + ": configuration loaded once");
        } else if (s->config_schema != schemaText)
            return BC_INVALID_ARGUMENT;
        return copy_string(s->config_text, out, capacity, required);
    } catch (const std::exception &e) {
        log(std::string("Configuration rejected: ") + e.what());
        return BC_INVALID_ARGUMENT;
    } catch (...) {
        return BC_INTERNAL;
    }
}
static FileChange &ini_file(Scope &s, const char *binding) {
    if (!binding || strnlen_s(binding, 129) > 128)
        throw std::runtime_error("Invalid file binding");
    auto it = s.manifest.ini_bindings.find(binding);
    if (it == s.manifest.ini_bindings.end())
        throw std::runtime_error("Undeclared INI binding");
    const auto path = (server_config_root / it->second).lexically_normal();
    for (auto &f : s.files)
        if (f.path == path)
            return f;
    auto text = read_bounded(path, 1048576);
    s.files.push_back({path, text, text});
    return s.files.back();
}
static bool ini_arguments(const char *section, const char *key, const char *value = "") {
    return section && key && value && strnlen_s(section, 1025) <= 1024 && strnlen_s(key, 257) <= 256 &&
           strnlen_s(value, 4097) <= 4096;
}
static BcResult BC_CALL read_ini(void *c, const char *binding, const char *section, const char *key,
                                 char *out, uint32_t capacity, uint32_t *required) noexcept {
    try {
        auto *s = scope(c, BC_CAP_INI);
        if (GetCurrentThreadId() != loading_thread || !s || loading_scope != s || !startup_phase)
            return BC_DENIED;
        if (!ini_arguments(section, key))
            return BC_INVALID_ARGUMENT;
        return copy_string(ini_read(ini_file(*s, binding).after, section, key), out, capacity, required);
    } catch (const std::exception &e) {
        log(std::string("INI read rejected: ") + e.what());
        return BC_INVALID_ARGUMENT;
    } catch (...) {
        return BC_INTERNAL;
    }
}
static BcResult BC_CALL stage_ini(void *c, const char *binding, const char *section, const char *key,
                                  const char *value) noexcept {
    try {
        auto *s = scope(c, BC_CAP_INI);
        if (GetCurrentThreadId() != loading_thread || !s || loading_scope != s || !startup_phase)
            return BC_DENIED;
        if (!ini_arguments(section, key, value))
            return BC_INVALID_ARGUMENT;
        auto &file = ini_file(*s, binding);
        file.after = ini_write(file.after, section, key, value);
        return BC_OK;
    } catch (const std::exception &e) {
        log(std::string("INI staging rejected: ") + e.what());
        return BC_INVALID_ARGUMENT;
    } catch (...) {
        return BC_INTERNAL;
    }
}
static BcResult BC_CALL stage_i32(void *c, const char *sha, const BcImmediatePatch *requests,
                                  uint32_t count) noexcept {
    try {
        auto *s = scope(c, BC_CAP_STARTUP_PATCH);
        if (GetCurrentThreadId() != loading_thread || !s || loading_scope != s || !startup_phase)
            return BC_DENIED;
        if (!sha || strnlen_s(sha, 65) != 64 || std::strcmp(sha, build_info.executable_sha256) != 0)
            return BC_VERSION_MISMATCH;
        if (!requests || !count || count > 64 || !s->patches.empty())
            return BC_INVALID_ARGUMENT;
        s->patches = validate_patches(game_image, build_info.image_size, {requests, count});
        for (const auto &p : s->patches)
            log(std::format("{}: validated MOV imm32 RVA=0x{:X}, {} -> {}", s->manifest.id, p.rva, p.before,
                            p.after));
        return BC_OK;
    } catch (const std::exception &e) {
        log(std::string("Patch staging rejected: ") + e.what());
        return BC_INVALID_ARGUMENT;
    } catch (...) {
        return BC_INTERNAL;
    }
}
#include "UnrealServices.inc"
static BcResult BC_CALL cs_snapshot(void *c,char *buffer,uint32_t capacity,uint64_t *revision) noexcept {
    try {
        auto *s=client_scope(c,BC_CAP_CONFIG);if(!s)return BC_DENIED;
        if(!buffer||capacity<2||!revision)return BC_INVALID_ARGUMENT;
        buffer[0]=0;std::string text;auto next=*revision;
        if(!s->client_settings.current(text,next))return BC_OK;
        if(text.size()>=capacity)return BC_LIMIT;
        memcpy(buffer,text.c_str(),text.size()+1);*revision=next;return BC_OK;
    }catch(...){return BC_INTERNAL;}
}
static BcResult BC_CALL cs_acknowledge(void *c,uint64_t revision) noexcept {
    try {auto *s=client_scope(c,BC_CAP_CONFIG);if(!s)return BC_DENIED;s->client_settings.acknowledge(revision);return BC_OK;}catch(...){return BC_INTERNAL;}
}
static BcResult BC_CALL cs_report(void *c,const char *key,const char *text,uint32_t severity) noexcept {
 try {auto *s=client_scope(c,BC_CAP_CONFIG);if(!s)return BC_DENIED;
 if(!key||!text||severity>3||strnlen_s(key,129)>128||strnlen_s(text,513)>512)return BC_INVALID_ARGUMENT;
 s->client_settings.report("mods."+s->manifest.id+"."+key,text,severity);return BC_OK;
 }catch(...){return BC_INTERNAL;}
}
static const BcClientSettingsApi client_settings_service{sizeof(BcClientSettingsApi),1,cs_snapshot,cs_acknowledge,cs_report};
static const BcConfigApi config_service = {sizeof(BcConfigApi), 1, config_load};
static const BcStartupApi startup_service = {sizeof(BcStartupApi), 1, read_ini, stage_ini, stage_i32};
static BcResult BC_CALL api_service(void *c, const char *name, uint32_t version, const void **out) noexcept {
    if (!scope(c))
        return BC_DENIED;
    if (!name || !out || strnlen_s(name, 129) > 128)
        return BC_INVALID_ARGUMENT;
    *out = nullptr;
    if (version != 1)
        return BC_VERSION_MISMATCH;
    if(std::strcmp(name,BC_CLIENT_SETTINGS_SERVICE)==0) {
        auto *s=client_scope(c,BC_CAP_CONFIG);if(!s)return BC_DENIED;
        s->client_settings.enable_live();
        *out=&client_settings_service;return BC_OK;
    }
    if (std::strcmp(name,BC_CLIENT_RENDER_SERVICE)==0) {
        if(!client_scope(c,BC_CAP_CLIENT_RENDER)) return BC_DENIED;
        *out=&client_render_service; return BC_OK;
    }
    if (std::strcmp(name,BC_CLIENT_INPUT_SERVICE)==0) {
        if(!client_scope(c,BC_CAP_CLIENT_INPUT)) return BC_DENIED;
        *out=&client_input_service; return BC_OK;
    }
    if (std::strcmp(name, BC_NATIVE_HOOK_SERVICE) == 0) {
        if (!scope(c, BC_CAP_NATIVE_HOOKS))
            return BC_DENIED;
        *out = &native_service;
        return BC_OK;
    }
    if (std::strcmp(name, BC_UNREAL_SERVICE) == 0) {
        auto *s = scope(c);
        if (!(s->manifest.capabilities &
              (BC_CAP_REFLECTION | BC_CAP_INVOCATION | BC_CAP_HOOKS | BC_CAP_LIFECYCLE)))
            return BC_DENIED;
        *out = &unreal_service;
        return BC_OK;
    }
    if (std::strcmp(name, BC_CONFIG_SERVICE) == 0) {
        if (!scope(c, BC_CAP_CONFIG))
            return BC_DENIED;
        *out = &config_service;
        return BC_OK;
    }
    if (std::strcmp(name, BC_STARTUP_SERVICE) == 0) {
        auto *s = scope(c);
        if (!startup_phase || s->manifest.phase != "startup" ||
            !(s->manifest.capabilities & (BC_CAP_INI | BC_CAP_STARTUP_PATCH)))
            return BC_DENIED;
        *out = &startup_service;
        return BC_OK;
    }
    return BC_NOT_FOUND;
}
