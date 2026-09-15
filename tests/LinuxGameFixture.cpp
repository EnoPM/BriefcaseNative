#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../runtime/Briefcase.Admin/Management.hpp"
#include <fstream>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
static volatile sig_atomic_t stopped;
static void stop(int) { stopped=1; }
int main(int argc, char** argv) {
    if (!std::getenv("BC_TEST_HOST_STARTED") || std::getenv("LD_PRELOAD")) return 90;
    if (argc < 3 || std::strcmp(argv[1], "DeceiveInc") || std::strcmp(argv[argc-1], "sentinel")) return 91;
    if (const auto marker=std::getenv("BC_TEST_RESTART")) {
        std::signal(SIGINT,stop);
        if (!(fcntl(std::atoi(std::getenv("BRIEFCASE_SUPERVISOR_FD")),F_GETFD)&FD_CLOEXEC)) return 92;
        const bool second=std::filesystem::exists(marker);
        { std::ofstream file(marker,std::ios::app);file<<getpid()<<'\n'; }
        if (!second) {
            const auto root=std::filesystem::current_path()/"Briefcase";
            const auto first=bc::admin::schedule_restart(root);
            const auto duplicate=bc::admin::schedule_restart(root);
            if(first["requestId"]!=duplicate["requestId"])return 93;
            bc::admin::commit_restart();
        } else if(!std::getenv("BC_TEST_WAIT"))return 0;
        for(int i=0;i<1000 && !stopped;++i)usleep(10000);
        return stopped?0:94;
    }
    std::puts("PASS host-before-main; preload cleared; arguments preserved");
    return 0;
}
