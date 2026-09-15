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
// Each TLS worker commits only the preparation whose response it has sent.
thread_local std::string acknowledged_id;
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
Json schedule_restart(const fs::path& root) {
    std::lock_guard lock(restart_mutex);
    if(platform::executable()!=root.parent_path()/"DeceiveIncServer-Linux-Shipping")
        throw Error("unavailable","Restart is restricted to the dedicated server.");
    if(!request_id.empty() && std::chrono::steady_clock::now()-prepared_at<std::chrono::seconds(4)) {
        acknowledged_id=request_id;
        return {{"scheduled",true},{"requestId",request_id}};
    }
    request_id.clear();supervisor_fd=channel();
    const auto candidate=hex(random_bytes(16));
    const auto request="prepare "+candidate;
    if(send(supervisor_fd,request.data(),request.size(),MSG_NOSIGNAL|MSG_DONTWAIT)!=ssize_t(request.size()))
        throw Error("unavailable","Supervisor did not accept the restart request.");
    pollfd descriptor{supervisor_fd,POLLIN,0};char response[128]{};
    if(poll(&descriptor,1,2000)<=0)throw Error("unavailable","Supervisor acknowledgement timed out.");
    const auto size=recv(supervisor_fd,response,sizeof(response),MSG_DONTWAIT);
    if(size<=0 || std::string_view(response,size)!="ready "+candidate)
        throw Error("unavailable","Supervisor acknowledgement rejected.");
    request_id=candidate;acknowledged_id=candidate;prepared_at=std::chrono::steady_clock::now();
    return {{"scheduled",true},{"requestId",request_id}};
}
void commit_restart() noexcept {
    try {
        std::lock_guard lock(restart_mutex);
        if(supervisor_fd<0 || request_id.empty() || acknowledged_id!=request_id)return;
        const auto message="commit "+request_id;
        send(supervisor_fd,message.data(),message.size(),MSG_NOSIGNAL|MSG_DONTWAIT);request_id.clear();acknowledged_id.clear();
    } catch(...) {}
}
}
