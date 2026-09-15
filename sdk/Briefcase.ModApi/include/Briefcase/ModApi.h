#ifndef BRIEFCASE_MOD_API_H
#define BRIEFCASE_MOD_API_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BC_API_VERSION 1u
#if defined(_WIN32)
#define BC_CALL __cdecl
#define BC_EXPORT __declspec(dllexport)
#elif defined(__linux__) && defined(__x86_64__)
#define BC_CALL
#define BC_EXPORT __attribute__((visibility("default")))
#else
#error Unsupported Briefcase platform
#endif
typedef int32_t BcResult;
typedef uint64_t BcHandle;
#define BC_OK 0
#define BC_INVALID_ARGUMENT 1
#define BC_VERSION_MISMATCH 2
#define BC_WRONG_THREAD 3
#define BC_NOT_FOUND 4
#define BC_STALE_HANDLE 5
#define BC_DENIED 6
#define BC_NOT_READY 7
#define BC_LIMIT 8
#define BC_INTERNAL 9
#define BC_CAP_LOG 1ull
#define BC_CAP_FIND 2ull
#define BC_CAP_SCHEDULE 4ull
typedef struct BcBuild {
    uint32_t size;
    uint32_t api_version;
    uint32_t pe_timestamp;
    uint32_t image_size;
    uint32_t engine_major;
    uint32_t engine_minor;
    char framework_version[32];
    char executable_sha256[65];
} BcBuild;
typedef void(BC_CALL *BcTask)(void *user);
typedef struct BcApi {
    uint32_t size;
    uint32_t version;
    uint64_t capabilities;
    void *context;
    BcResult(BC_CALL *log)(void *context, uint32_t level, const char *utf8, uint32_t length);
    BcResult(BC_CALL *get_build)(void *context, BcBuild *out);
    BcResult(BC_CALL *find_object)(void *context, const char *path_utf8, uint32_t length, BcHandle *out);
    BcResult(BC_CALL *validate_handle)(void *context, BcHandle handle);
    BcResult(BC_CALL *release_handle)(void *context, BcHandle handle);
    BcResult(BC_CALL *post_game_thread)(void *context, BcTask task, void *user);
    /* Optional tail: check size before reading. Original 72-byte v1 prefix is unchanged. */
    BcResult(BC_CALL *get_service)(void *context, const char *name, uint32_t version, const void **out);
} BcApi;
/* Ready-phase entry runs on a worker; startup entry runs before the EXE entry.
   Unreal access requires post_game_thread after backend readiness.
   API/context live until process exit. No hot unload.
   Callback and user data belong to the mod. Never throw across this ABI. */
typedef BcResult(BC_CALL *BcModLoad)(const BcApi *api);
#ifdef __cplusplus
}
#endif
#endif
