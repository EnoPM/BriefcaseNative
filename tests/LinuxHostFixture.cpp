#include <cstdlib>
#include <cstdint>
#include <unistd.h>
extern "C" __attribute__((visibility("default"))) uint32_t BriefcaseLinuxStart(uint32_t thread) {
    if (thread != static_cast<uint32_t>(gettid()) || std::getenv("BC_TEST_REJECT")) return 1;
    setenv("BC_TEST_HOST_STARTED", "1", 1);
    return 0;
}
