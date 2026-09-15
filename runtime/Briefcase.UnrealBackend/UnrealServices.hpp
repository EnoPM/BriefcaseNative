#pragma once
#include <Briefcase/NativeHookApi.h>
#include <Briefcase/UnrealApi.h>
namespace bc {
BcResult backend_resolve(uint64_t, const char *, const char *, BcHandle *);
BcResult backend_describe(uint64_t, BcHandle, char *, uint32_t, uint32_t *);
BcResult backend_info(uint64_t, BcHandle, BcObjectInfo *);
BcResult backend_retain(uint64_t, BcHandle);
BcResult backend_read_property(uint64_t, BcHandle, const char *, BcValue *);
BcResult backend_write_property(uint64_t, BcHandle, const char *, const BcValue *);
BcResult backend_write_argument(uint64_t, BcHandle, const char *, const BcValue *);
BcResult backend_invoke(uint64_t, BcHandle, BcHandle, const BcNamedValue *, uint32_t, BcValue *);
BcResult backend_hook(uint64_t, BcHandle, uint32_t, BcHookCallback, void *, BcHandle *);
BcResult backend_unhook(uint64_t, BcHandle);
BcResult backend_read_argument(uint64_t, BcHandle, const char *, BcValue *);
BcResult backend_deleted(uint64_t, BcDeletedCallback, void *, BcHandle *);
BcResult backend_enum(uint64_t, const char *, const char *, int64_t *);
BcResult backend_native_hook(uint64_t, BcHandle, const BcNativeSite *, uint32_t, BcHookCallback, void *,
                             BcHandle *);
BcResult backend_invoke_outputs(uint64_t, BcHandle, BcHandle, const BcNamedValue *, uint32_t, BcNamedValue *, uint32_t, BcValue *);
BcResult backend_enumerate_actors(uint64_t, BcHandle, const char *, BcHandle *, uint32_t, uint32_t *);
BcResult backend_is_a(uint64_t, BcHandle, const char *, uint32_t *);
void backend_cleanup_owner(uint64_t);
} // namespace bc
