#ifndef BRIEFCASE_CLIENT_MOD_API_H
#define BRIEFCASE_CLIENT_MOD_API_H
#include <Briefcase/ModApi.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BC_CLIENT_RENDER_SERVICE "briefcase.client.render"
#define BC_CLIENT_INPUT_SERVICE "briefcase.client.input"
#define BC_CAP_CLIENT_RENDER 4096ull
#define BC_CAP_CLIENT_INPUT 8192ull
typedef struct BcViewport {
    uint32_t size, width, height;
    float dpi_scale;
    uint64_t frame_number;
    double delta_seconds;
} BcViewport;
typedef struct BcClientFrame {
    uint32_t size, menu_open, input_captured, focused; /* focused replaces reserved, same layout */
    BcViewport viewport;
} BcClientFrame;
typedef void(BC_CALL *BcRenderCallback)(const BcClientFrame *, void *user);
typedef struct BcClientRenderApi {
    uint32_t size, version;
    BcResult(BC_CALL *subscribe)(void *, BcRenderCallback, void *user, BcHandle *);
    BcResult(BC_CALL *unsubscribe)(void *, BcHandle);
    BcResult(BC_CALL *viewport)(void *, BcViewport *);
    /* Convert normalized [0,1] coordinates to physical back-buffer pixels. */
    BcResult(BC_CALL *to_pixels)(void *, double normalized_x, double normalized_y, float *x, float *y);
    /* Drawing only inside the owning render callback. Colors use 0xAABBGGRR. */
    BcResult(BC_CALL *text)(void *, float x, float y, uint32_t color, const char *utf8, uint32_t length);
    BcResult(BC_CALL *rectangle)(void *, float x, float y, float width, float height, uint32_t color);
    /* Optional v1 tail; width/thickness/radius are physical pixels. */
    BcResult(BC_CALL *line)(void *, float, float, float, float, uint32_t, float);
    BcResult(BC_CALL *circle)(void *, float, float, float, uint32_t, float);
} BcClientRenderApi;
typedef struct BcKeyState {
    uint32_t size, down, pressed, released;
    uint64_t frame_number;
} BcKeyState;
typedef struct BcClientInputApi {
    uint32_t size, version;
    /* Windows virtual-key codes, 1..255. Pressed/released are stable for the current frame. */
    BcResult(BC_CALL *key)(void *, uint32_t virtual_key, BcKeyState *);
    BcResult(BC_CALL *capturing)(void *, uint32_t *);
} BcClientInputApi;
#define BC_CLIENT_SETTINGS_SERVICE "briefcase.client.settings"
typedef struct BcClientSettingsApi {
    uint32_t size,version;
    /* Call config.load during entry first. A revision of zero fetches initial values.
       No file IO. Empty string means unchanged. Acknowledge after applying on game thread. */
    BcResult(BC_CALL *snapshot)(void *,char *,uint32_t,uint64_t *);
    BcResult(BC_CALL *acknowledge)(void *,uint64_t);
    /* Optional, bounded status. Key is relative to the owning mod translation namespace.
       Severity: 0 info, 1 success, 2 warning, 3 danger. Never include secrets. */
    BcResult(BC_CALL *report)(void *,const char *key,const char *fallback,uint32_t severity);
} BcClientSettingsApi;
/* Client-only services; server returns BC_DENIED without loading any client DLL.
   One framework-owned ImGui context; no graphics/Unreal object crosses this ABI.
   Callbacks run on the render thread, never access Unreal there.
   Unsubscribe during a callback removes future calls; user storage must outlive an in-flight call.
   Framework cleanup deactivates callbacks before mod unload. DLLs remain mapped until exit.
   No exception may cross a DLL boundary. */
#ifdef __cplusplus
}
#endif
#endif
