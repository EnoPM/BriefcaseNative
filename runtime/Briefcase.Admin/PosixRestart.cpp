#include "Management.hpp"
#include "../Briefcase.NativeHost/Platform.hpp"
#include <charconv>
#include <cstdlib>
#include <chrono>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
namespace bc::admin {
namespace {
std::mutex restart_mutex;
std::string request_id;
bool request_shutdown{};
// Each TLS worker commits only the preparation whose response it has sent.
thread_local std::string acknowledged_id;
thread_local bool acknowledged_shutdown{};
int supervisor_fd=-1;
std::chrono::steady_clock::time_point prepared_at;
int number(const char* name) {
    const char* text=std::getenv(name);if(!text)throw Error("unavailable","Server was not started by its supervisor.");
    int value{};const auto end=text+std::char_traits<char>::length(text);
    const auto parsed=std::from_chars(text,end,value);
    if(parsed.ec!=std::errc{} || parsed.ptr!=end || value<0)throw Error("unavailable","Invalid supervisor identity.");
    return value;
}
int channel() {
    const int fd=number("BRIEFCASE_SUPERVISOR_FD"),pid=number("BRIEFCASE_SUPERVISOR_PID");
    ucred peer{};socklen_t length=sizeof(peer);
    int type{};socklen_t type_length=sizeof(type);
    if(fd<3 || getppid()!=pid || getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&length) ||
       peer.pid!=pid || peer.uid!=geteuid() ||
       getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&type_length) || type!=SOCK_SEQPACKET)
        throw Error("unavailable","Supervisor channel identity mismatch.");
    return fd;
}
}
Json schedule_action(const fs::path& root, bool shutdown) {
    std::lock_guard lock(restart_mutex);
    if(platform::executable()!=root.parent_path()/"DeceiveIncServer-Linux-Shipping")
        throw Error("unavailable","The process action is restricted to the dedicated server.");
    if(!request_id.empty() && request_shutdown==shutdown &&
       std::chrono::steady_clock::now()-prepared_at<std::chrono::seconds(4)) {
        acknowledged_id=request_id;acknowledged_shutdown=shutdown;
        return {{"scheduled",true},{"requestId",request_id}};
    }
    request_id.clear();supervisor_fd=channel();
    const auto candidate=hex(random_bytes(16));
    const auto request=(shutdown ? "prepare-stop " : "prepare ")+candidate;
    if(send(supervisor_fd,request.data(),request.size(),MSG_NOSIGNAL|MSG_DONTWAIT)!=ssize_t(request.size()))
        throw Error("unavailable","Supervisor did not accept the process request.");
    pollfd descriptor{supervisor_fd,POLLIN,0};char response[128]{};
    if(poll(&descriptor,1,2000)<=0)throw Error("unavailable","Supervisor acknowledgement timed out.");
    const auto size=recv(supervisor_fd,response,sizeof(response),MSG_DONTWAIT);
    const auto expected=(shutdown ? "ready-stop " : "ready ")+candidate;
    if(size<=0 || std::string_view(response,size)!=expected)
        throw Error("unavailable","Supervisor acknowledgement rejected.");
    request_id=candidate;request_shutdown=shutdown;acknowledged_id=candidate;
    acknowledged_shutdown=shutdown;prepared_at=std::chrono::steady_clock::now();
    return {{"scheduled",true},{"requestId",request_id}};
}
Json schedule_restart(const fs::path& root) { return schedule_action(root,false); }
Json schedule_shutdown(const fs::path& root) { return schedule_action(root,true); }
void commit_restart() noexcept {
    try {
        std::lock_guard lock(restart_mutex);
        if(supervisor_fd<0 || request_id.empty() || acknowledged_id!=request_id ||
           acknowledged_shutdown!=request_shutdown)return;
        const auto message=(request_shutdown ? "commit-stop " : "commit ")+request_id;
        send(supervisor_fd,message.data(),message.size(),MSG_NOSIGNAL|MSG_DONTWAIT);
        request_id.clear();acknowledged_id.clear();request_shutdown=false;acknowledged_shutdown=false;
    } catch(...) {}
}
}
