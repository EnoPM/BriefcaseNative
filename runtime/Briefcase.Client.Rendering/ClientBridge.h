#pragma once
#include <Briefcase/ClientModApi.h>
enum : uint32_t { BC_UI_PENDING = 0, BC_UI_LOADED = 1, BC_UI_DISABLED = 2, BC_UI_ERROR = 3 };
struct BcClientHome {
    uint32_t size, discovered, loaded, unreal_state;
    BcBuild build;
    double bootstrap_ms, unreal_ms;
};
struct BcClientModRow {
    uint32_t size, state;
    char id[128], name[256], author[128], version[32], environment[16];
    char error[512], dependencies[2048];
};
struct BcClientMetrics {
    uint32_t size, graphics_state, menu_open, context_created;
    uint64_t frames, closed_frames, open_frames, dormant_frames;
    double hook_ms, context_ms, fonts_ms, resources_ms;
    double closed_average_us, open_average_us, dormant_average_us;
    uint32_t width, height, callback_count, device_errors;
};
struct BcClientServerRow {
    uint64_t id;
    char name[96], endpoint[256];
};
struct BcClientServerList {
    uint32_t size, count, pending, writable, join_state;
    char message[512], join_message[512];
    BcClientServerRow entries[64];
};
struct BcClientHostApi {
    uint32_t size, version;
    void(BC_CALL *log)(const char *);
    BcResult(BC_CALL *home)(BcClientHome *);
    BcResult(BC_CALL *mod)(uint32_t, BcClientModRow *);
    void(BC_CALL *request_unreal)();
    void(BC_CALL *request_shutdown)();
    uint32_t(BC_CALL *startup_ready)();
    BcResult(BC_CALL *servers)(BcClientServerList *);
    BcResult(BC_CALL *add_server)(const char *, const char *);
    BcResult(BC_CALL *remove_server)(uint64_t);
    BcResult(BC_CALL *join_server)(uint64_t);
    // Private framework bridge v7; the public mod ABI is unchanged.
    BcResult(BC_CALL *admin_select)(uint64_t);
    BcResult(BC_CALL *admin_connect)(uint64_t, const char *, const char *, const char *, uint32_t);
    BcResult(BC_CALL *admin_command)(const char *, const char *);
    void(BC_CALL *admin_disconnect)();
    BcResult(BC_CALL *admin_snapshot)(char *, uint32_t, uint64_t *);
    BcResult(BC_CALL *locale_snapshot)(char *, uint32_t, uint64_t *);
    BcResult(BC_CALL *locale_select)(const char *);
    BcResult(BC_CALL *mod_settings)(const char *, char *, uint32_t);
    BcResult(BC_CALL *save_mod_settings)(const char *, uint64_t, const char *);
    uint32_t(BC_CALL *menu_key)();
    BcResult(BC_CALL *set_menu_key)(uint32_t);
};
struct BcClientModuleApi {
    uint32_t size, version;
    BcResult(BC_CALL *start)(const BcClientHostApi *);
    void(BC_CALL *stop)();
    void(BC_CALL *cleanup_owner)(uint64_t);
    BcResult(BC_CALL *subscribe)(uint64_t, BcRenderCallback, void *, BcHandle *);
    BcResult(BC_CALL *unsubscribe)(uint64_t, BcHandle);
    BcResult(BC_CALL *viewport)(BcViewport *);
    BcResult(BC_CALL *text)(uint64_t, float, float, uint32_t, const char *, uint32_t);
    BcResult(BC_CALL *rectangle)(uint64_t, float, float, float, float, uint32_t);
    BcResult(BC_CALL *key)(uint32_t, BcKeyState *);
    uint32_t(BC_CALL *capturing)();
    BcResult(BC_CALL *metrics)(BcClientMetrics *);
    BcResult(BC_CALL *line)(uint64_t, float, float, float, float, uint32_t, float);
    BcResult(BC_CALL *circle)(uint64_t, float, float, float, uint32_t, float);
};
using BcGetClientModuleApi = const BcClientModuleApi *(BC_CALL *)();
