#include <Briefcase/NativeHookApi.h>
#include <Briefcase/StartupApi.h>
#include <Briefcase/UnrealApi.h>
#include <stddef.h>
_Static_assert(sizeof(BcResult) == 4, "Result must have a fixed 32-bit ABI");
_Static_assert(sizeof(BcHandle) == 8, "Handles must have a fixed 64-bit ABI");
_Static_assert(offsetof(BcApi, context) == 16, "API prefix layout changed");
_Static_assert(offsetof(BcApi, get_service) == 72, "Original API v1 prefix changed");
_Static_assert(sizeof(BcApi) == 80, "Extended v1 layout changed");
_Static_assert(sizeof(BcBuild) == 124, "Build identity layout changed");
_Static_assert(sizeof(BcImmediatePatch) == 32, "Immediate descriptor ABI");
_Static_assert(sizeof(BcConfigApi) == 16, "Config service ABI");
_Static_assert(sizeof(BcStartupApi) == 32, "Startup service ABI");
_Static_assert(sizeof(BcValue) == 32, "Value ABI");
_Static_assert(sizeof(BcHookEvent) == 40, "Hook ABI");
_Static_assert(offsetof(BcUnrealApi, write_property) == 96, "Unreal v1 prefix preserved");
_Static_assert(offsetof(BcUnrealApi, invoke_outputs) == 112, "Unreal extended service ABI");
int main(void) {
    return BC_API_VERSION != 1;
}

_Static_assert(sizeof(BcNativeSite) == 40, "Native descriptor ABI");
_Static_assert(sizeof(BcNativeHookApi) == 16, "Native service ABI");

#include <Briefcase/ClientModApi.h>
_Static_assert(sizeof(BcViewport)==32,"client viewport C layout");
_Static_assert(sizeof(BcClientFrame)==48,"client frame C layout");
_Static_assert(sizeof(BcKeyState)==24,"client input C layout");
_Static_assert(offsetof(BcClientRenderApi,line)==56,"client render C layout");
_Static_assert(sizeof(BcClientInputApi)==24,"client input service C layout");
