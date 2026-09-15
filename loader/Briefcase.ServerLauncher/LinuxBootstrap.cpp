// A small ELF preload bridge. C++ runtime initialization has completed when main
// is entered; no UE4SS work is performed in a shared-library constructor.
#include <dlfcn.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <cstdint>
#include <fcntl.h>

using Main = int (*)(int, char**, char**);
static Main game_main;
static int enter_game(int argc, char** argv, char** envp) {
    char exe[PATH_MAX]{};
    const auto size = readlink("/proc/self/exe", exe, sizeof(exe)-1);
    const char* expected = std::getenv("BRIEFCASE_SERVER_EXECUTABLE");
    const char* host_path = std::getenv("BRIEFCASE_NATIVE_HOST");
    if (size <= 0 || !expected || !host_path || std::strcmp(exe, expected)) {
        std::fputs("Briefcase: invalid Linux launcher target\n", stderr);
        return 78;
    }
    // The launcher disallows a pre-existing LD_PRELOAD. Children inherit no bridge.
    unsetenv("LD_PRELOAD");
    if (const char* descriptor = std::getenv("BRIEFCASE_SUPERVISOR_FD")) {
        char* end{};
        const long fd = std::strtol(descriptor, &end, 10);
        if (end != descriptor && !*end && fd >= 3 && fd <= INT_MAX) {
            const int flags = fcntl(static_cast<int>(fd), F_GETFD);
            if (flags >= 0) fcntl(static_cast<int>(fd), F_SETFD, flags | FD_CLOEXEC);
        }
    }
    if (const char* descriptor = std::getenv("BRIEFCASE_BOOTSTRAP_FD")) {
        char* end{};
        const long fd = std::strtol(descriptor, &end, 10);
        if (end != descriptor && !*end && fd >= 3 && fd <= INT_MAX) close(static_cast<int>(fd));
        unsetenv("BRIEFCASE_BOOTSTRAP_FD");
    }
    void* host = dlopen(host_path, RTLD_NOW | RTLD_LOCAL);
    if (!host) { std::fprintf(stderr, "Briefcase: %s\n", dlerror()); return 78; }
    using Start = uint32_t (*)(uint32_t);
    auto start = reinterpret_cast<Start>(dlsym(host, "BriefcaseLinuxStart"));
    if (!start || start(static_cast<uint32_t>(gettid()))) return 78;
    return game_main(argc, argv, envp);
}
extern "C" __attribute__((visibility("default")))
int __libc_start_main(Main main, int argc, char** argv, void (*init)(), void (*fini)(),
                      void (*rtld_fini)(), void* stack_end) {
    using Start = int (*)(Main,int,char**,void(*)(),void(*)(),void(*)(),void*);
    auto real = reinterpret_cast<Start>(dlsym(RTLD_NEXT, "__libc_start_main"));
    if (!real) _exit(78);
    game_main = main;
    return real(enter_game, argc, argv, init, fini, rtld_fini, stack_end);
}
