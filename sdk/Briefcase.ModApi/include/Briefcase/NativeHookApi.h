#ifndef BRIEFCASE_NATIVE_HOOK_API_H
#define BRIEFCASE_NATIVE_HOOK_API_H
#include "UnrealApi.h"
#ifdef __cplusplus
extern "C" {
#endif
#define BC_NATIVE_HOOK_SERVICE "briefcase.native-hooks"
#define BC_CAP_NATIVE_HOOKS 1024ull
#define BC_TRANSPORT_NATIVE 3u
/* RVAs are relative to the PE module base or the first offset-zero ELF PT_LOAD. */
typedef struct BcNativeSite {
    uint32_t size, exec_rva, branch_offset, exec_size;
    const uint8_t *exec_bytes;
    uint32_t target_rva, target_size;
    const uint8_t *target_bytes;
} BcNativeSite;
typedef struct BcNativeHookApi {
    uint32_t size, version;
    BcResult(BC_CALL *hook)(void *, BcHandle function, const char *executable_sha256, const BcNativeSite *,
                            uint32_t phase, BcHookCallback, void *user, BcHandle *registration);
} BcNativeHookApi;
/* Native hooks require native.hooks + unreal.hooks + unreal.reflection.
   The exact exec wrapper, its relative call/jump, and the target prefix must match.
   Initial shapes: void member functions with zero or one float/bool/object argument.
   Calls on other threads run vanilla without mod callbacks. No original suppression.
   Remove registrations through BcUnrealApi.unhook. DLLs/trampolines stay mapped until exit.
   The caller supplies build-specific sites; the host never guesses a native callee. */
#ifdef __cplusplus
}
#endif
#endif
