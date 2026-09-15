#pragma once
#include <Briefcase/NativeHookApi.h>
#include <cstdint>
namespace bc {
void validate_native_site(const uint8_t *image, uint32_t image_size, const BcNativeSite &site);
}
