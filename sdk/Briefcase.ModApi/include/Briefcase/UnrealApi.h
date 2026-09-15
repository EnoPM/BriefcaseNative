#ifndef BRIEFCASE_UNREAL_API_H
#define BRIEFCASE_UNREAL_API_H
#include "ModApi.h"
#ifdef __cplusplus
extern "C" {
#endif
#define BC_UNREAL_SERVICE "briefcase.unreal"
#define BC_CAP_REFLECTION 64ull
#define BC_CAP_INVOCATION 128ull
#define BC_CAP_HOOKS 256ull
#define BC_CAP_LIFECYCLE 512ull
#define BC_CAP_UNREAL_WRITE 2048ull
#define BC_UNSUPPORTED 10
#define BC_VALUE_NONE 0u
#define BC_VALUE_I32 1u
#define BC_VALUE_F32 2u
#define BC_VALUE_BOOL 3u
#define BC_VALUE_OBJECT 4u
#define BC_VALUE_U8 5u
#define BC_VALUE_VECTOR 6u
#define BC_VALUE_ROTATOR 7u
#define BC_VALUE_VECTOR2 8u
#define BC_HOOK_PRE 1u
#define BC_HOOK_POST 2u
#define BC_TRANSPORT_EVENT 1u
#define BC_TRANSPORT_THUNK 2u
typedef struct BcValue {
    uint32_t kind, reserved;
    union {
        int64_t integer;
        double number;
        BcHandle object;
        double vector[3];
    } data;
} BcValue;
typedef struct BcNamedValue {
    const char *name;
    BcValue value;
} BcNamedValue;
typedef struct BcObjectInfo {
    uint32_t size, flags;
    char path[1024];
    char class_path[512];
} BcObjectInfo;
typedef struct BcHookEvent {
    uint32_t size, phase;
    BcHandle self;     /* borrowed; retain to keep it after callback */
    BcHandle function; /* borrowed */
    BcHandle call;     /* transient, valid only during this callback */
    uint32_t transport;
    uint32_t parameters_available;
} BcHookEvent;
typedef void(BC_CALL *BcHookCallback)(const BcHookEvent *event, void *user);
typedef void(BC_CALL *BcDeletedCallback)(BcHandle stale_object, void *user);
typedef struct BcUnrealApi {
    uint32_t size, version;
    BcResult(BC_CALL *resolve_function)(void *, const char *path, const char *signature_json, BcHandle *);
    BcResult(BC_CALL *describe_function)(void *, BcHandle, char *out, uint32_t capacity, uint32_t *required);
    BcResult(BC_CALL *object_info)(void *, BcHandle, BcObjectInfo *);
    BcResult(BC_CALL *retain)(void *, BcHandle);
    BcResult(BC_CALL *read_property)(void *, BcHandle, const char *name, BcValue *);
    BcResult(BC_CALL *invoke)(void *, BcHandle self, BcHandle function, const BcNamedValue *arguments,
                              uint32_t count, BcValue *result);
    BcResult(BC_CALL *hook)(void *, BcHandle function, uint32_t phase, BcHookCallback callback, void *user,
                            BcHandle *registration);
    BcResult(BC_CALL *unhook)(void *, BcHandle registration);
    BcResult(BC_CALL *read_argument)(void *, BcHandle call, const char *name, BcValue *);
    BcResult(BC_CALL *on_deleted)(void *, BcDeletedCallback, void *user, BcHandle *registration);
    BcResult(BC_CALL *enum_value)(void *, const char *path, const char *name, int64_t *out);
    /* Appended v1 extension: live-instance scalar/vector writes; no CDO/archetype/object writes.
       Argument writes require PRE, a materialized buffer, and an input parameter.
       Requires unreal.write plus reflection (property) or hooks (argument). */
    BcResult(BC_CALL *write_property)(void *, BcHandle, const char *name, const BcValue *);
    BcResult(BC_CALL *write_argument)(void *, BcHandle call, const char *name, const BcValue *);
    /* Optional tail. Outputs are named, scalar POD only; returned object handles are owned.
       enumerate_actors uses the supplied world; world=0 discovers live instances globally.
       Returned handles must be released even when fewer than capacity are returned. */
    BcResult(BC_CALL *invoke_outputs)(void *, BcHandle, BcHandle, const BcNamedValue *, uint32_t,
                                     BcNamedValue *, uint32_t, BcValue *);
    BcResult(BC_CALL *enumerate_actors)(void *, BcHandle world, const char *class_path,
                                       BcHandle *, uint32_t capacity, uint32_t *count);
    BcResult(BC_CALL *is_a)(void *, BcHandle, const char *class_path, uint32_t *);
} BcUnrealApi;
/* All operations require the game thread and the declared capability.
   Numeric/object/vector values only in v1; unsupported layouts are refused.
   Object values returned by reads/invocation are owned handles and must be released.
   Reflected hooks observe ProcessEvent and Func thunks, with duplicate suppression.
   Direct calls to a C++ implementation are outside this transport.
   BriefcaseModUnload is optional, called on the game thread before owner cleanup.
   DLLs remain mapped. Forced process termination cannot run unload callbacks. */
typedef void(BC_CALL *BcModUnload)(void);
#ifdef __cplusplus
}
#endif
#endif
