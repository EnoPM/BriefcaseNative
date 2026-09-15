#ifndef BRIEFCASE_STARTUP_API_H
#define BRIEFCASE_STARTUP_API_H
#include "ModApi.h"
#ifdef __cplusplus
extern "C" {
#endif
#define BC_STARTUP_SERVICE "briefcase.startup"
#define BC_CONFIG_SERVICE "briefcase.config"
#define BC_CAP_CONFIG 8ull
#define BC_CAP_STARTUP_PATCH 16ull
#define BC_CAP_INI 32ull
/* Fixed .text window for one exact executable SHA256. Only a decoded MOV r32,imm32
   operand may change; no memory address is returned to the mod. */
typedef struct BcImmediatePatch {
    uint32_t size;
    uint32_t window_rva;
    const uint8_t *expected;
    uint32_t window_size;
    uint32_t operand_offset;
    int32_t replacement;
    uint32_t reserved;
} BcImmediatePatch;
typedef struct BcConfigApi {
    uint32_t size, version;
    /* Schema: object with properties; each declares type, default, description.
       Required output includes NUL. Size probe returns BC_LIMIT, caches one load. */
    BcResult(BC_CALL *load)(void *context, const char *schema, uint32_t schema_size, char *out,
                            uint32_t capacity, uint32_t *required);
} BcConfigApi;
typedef struct BcStartupApi {
    uint32_t size, version;
    BcResult(BC_CALL *read_ini)(void *context, const char *binding, const char *section, const char *key,
                                char *out, uint32_t capacity, uint32_t *required);
    BcResult(BC_CALL *stage_ini)(void *context, const char *binding, const char *section, const char *key,
                                 const char *value);
    BcResult(BC_CALL *stage_i32)(void *context, const char *sha256, const BcImmediatePatch *patches,
                                 uint32_t count);
} BcStartupApi;
/* Stage functions are legal only within startup BriefcaseModLoad. Host commits
   after BC_OK and discards/rolls back on failure, before the EXE entry executes.
   No hot unload: modified instructions cease to exist with the process. */
#ifdef __cplusplus
}
#endif
#endif
