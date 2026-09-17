#include "Update.hpp"
#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <poll.h>
#include <regex>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace bc::launcher {
namespace {
volatile sig_atomic_t stopping = 0;
void stop(int) { stopping = 1; }
struct Fd {
    int value = -1;
    explicit Fd(int fd) : value(fd) {}
    Fd(const Fd&) = delete;
    ~Fd() { if (value >= 0) close(value); }
};
fs::path executable() {
    std::array<char, 4096> bytes{};
    auto size = readlink("/proc/self/exe", bytes.data(), bytes.size());
    require(size > 0 && size < ssize_t(bytes.size()), "Cannot locate launcher");
    return plain(std::string(bytes.data(), size));
}
void log(const fs::path& root, const std::string& message) {
    const auto path = package_path(root, "Briefcase/Logs/launcher.log");
    fs::create_directories(path.parent_path()); plain(path);
    Fd fd(open(path.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0644));
    struct stat info{};
    require(fd.value >= 0 && fstat(fd.value, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1, "Cannot open launcher log");
    const auto line = std::to_string(std::time(nullptr)) + " " + message + "\n";
    size_t offset = 0;
    while (offset < line.size()) {
        auto size = write(fd.value, line.data() + offset, line.size() - offset);
        if (size < 0 && errno == EINTR) continue;
        require(size > 0, "Cannot write launcher log"); offset += size;
    }
}
bool game_running(const fs::path& game) {
    for (const auto& entry : fs::directory_iterator("/proc")) {
        const auto name = entry.path().filename().string();
        if (name.find_first_not_of("0123456789") != std::string::npos) continue;
        std::error_code error;
        if (fs::equivalent(entry.path() / "exe", game, error)) return true;
    }
    return false;
}
class Lock {
    Fd fd_;
    static int acquire(const fs::path& root) {
        auto path = package_path(root, "Briefcase/Updates/launch.lock");
        fs::create_directories(path.parent_path()); plain(path);
        int fd = open(path.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
        struct stat info{};
        if (fd < 0 || fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1 || flock(fd, LOCK_EX | LOCK_NB)) {
            if (fd >= 0) close(fd);
            throw std::runtime_error("Server launcher already running or unsafe lock");
        }
        return fd;
    }
public:
    explicit Lock(const fs::path& root) : fd_(acquire(root)) {}
};
class Child {
public:
    pid_t pid;
    bool reaped = false;
    explicit Child(pid_t value) : pid(value) {}
    ~Child() {
        if (!reaped) { kill(pid, SIGKILL); while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {} }
    }
};
pid_t launch(const fs::path& game, const std::vector<std::string>& arguments, int channel, int parent_channel) {
    const auto root = game.parent_path();
    const auto host = package_path(root, "Briefcase/Core/libBriefcase.NativeHost.so");
    const auto bridge = package_path(root, "Briefcase/Core/libBriefcase.ServerBootstrap.so");
    require(fs::is_regular_file(host) && fs::is_regular_file(bridge), "Runtime libraries missing");
    Fd bridge_fd(open(bridge.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    require(bridge_fd.value >= 0, "Cannot open bootstrap");
    std::vector<std::string> values{game.string(), "DeceiveInc", "-unattended", "-NoSplash", "-NOCONSOLE", "-nullrhi", "-nosound"};
    values.insert(values.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (auto& value : values) argv.push_back(value.data());
    argv.push_back(nullptr);
    const auto child = fork();
    require(child >= 0, "Cannot fork game");
    if (!child) {
        // The launcher is single-threaded; only these two descriptors survive exec.
        close(parent_channel);
        if (setsid() < 0 || chdir(root.c_str()) || fcntl(channel, F_SETFD, 0) || fcntl(bridge_fd.value, F_SETFD, 0)) _exit(78);
        auto env = [](const char* key, const std::string& value) { if (setenv(key, value.c_str(), 1)) _exit(78); };
        env("BRIEFCASE_SERVER_EXECUTABLE", game.string()); env("BRIEFCASE_NATIVE_HOST", host.string());
        env("BRIEFCASE_BOOTSTRAP_FD", std::to_string(bridge_fd.value)); env("BRIEFCASE_SUPERVISOR_FD", std::to_string(channel));
        env("BRIEFCASE_SUPERVISOR_PID", std::to_string(getppid())); env("LD_PRELOAD", "/proc/self/fd/" + std::to_string(bridge_fd.value));
        std::string libraries = root.parent_path().parent_path().parent_path().string();
        if (const auto previous = std::getenv("LD_LIBRARY_PATH"); previous && *previous) libraries += ":" + std::string(previous);
        env("LD_LIBRARY_PATH", libraries);
        std::signal(SIGINT, SIG_DFL); std::signal(SIGTERM, SIG_DFL);
        execv(game.c_str(), argv.data());
        std::fputs("Briefcase: cannot execute Shipping server\n", stderr); _exit(78);
    }
    return child;
}
int supervise(const fs::path& game, const std::vector<std::string>& arguments) {
    require(game.filename() == game_name && fs::is_regular_file(plain(game)) && access(game.c_str(), X_OK) == 0, "Invalid dedicated-server executable");
    const auto preload = std::getenv("LD_PRELOAD");
    require(!preload || !*preload, "An existing LD_PRELOAD is not supported");
    const auto root = game.parent_path();
    Lock lock(root);
    std::signal(SIGINT, stop); std::signal(SIGTERM, stop);
    while (!stopping) {
        require(!game_running(game), "Dedicated server already running");
        const auto result = Updater(root).update([&](const std::string& message) { log(root, message); });
        if (stopping) return 0;
        if (result == "installed") {
            Updater(root).cleanup([&](const std::string& message) { log(root, message); });
            // CLOEXEC releases the existing lock only if exec succeeds. No unlock gap.
            const auto target = package_path(root, "Briefcase.ServerLauncher");
            std::vector<std::string> values{target.string(), "--server", game.string(), "--"};
            values.insert(values.end(), arguments.begin(), arguments.end());
            std::vector<char*> argv;
            for (auto& value : values) argv.push_back(value.data());
            argv.push_back(nullptr);
            execv(target.c_str(), argv.data());
            throw std::runtime_error("Cannot restart updated launcher");
        }
        const auto mods = Updater(root).update_mods([&](const std::string& message) { log(root, message); });
        write_json(package_path(root, "Briefcase/Updates/last-result.json"),
                   {{"framework", result}, {"mods", mods}, {"checkedAt", std::time(nullptr)}});
        Updater(root).cleanup([&](const std::string& message) { log(root, message); });
        int pair[2]; require(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0, "Cannot create restart channel");
        Fd parent(pair[0]);
        pid_t pid;
        { Fd channel(pair[1]); pid = launch(game, arguments, channel.value, parent.value); }
        Child child(pid);
        log(root, "Server started pid=" + std::to_string(pid));
        bool restart = false, shutdown = false, channel_open = true, interrupt_sent = false, terminate_sent = false;
        std::string prepared, prepared_action;
        auto prepared_at = std::chrono::steady_clock::now(), stop_at = prepared_at;
        while (true) {
            int status{}; const auto wait = waitpid(pid, &status, WNOHANG);
            if (wait == pid) {
                child.reaped = true;
                const auto code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
                log(root, "Server exited code=" + std::to_string(code));
                if (restart && !stopping) break;
                if (shutdown) return 0;
                return code;
            }
            require(wait >= 0 || errno == EINTR, "Cannot wait for server");
            const auto now = std::chrono::steady_clock::now();
            if (stopping || restart || shutdown) {
                if (!interrupt_sent) { stop_at = now; kill(pid, SIGINT); interrupt_sent = true; }
                else if (!terminate_sent && now - stop_at > std::chrono::seconds(30)) { kill(pid, SIGTERM); terminate_sent = true; log(root, "Graceful stop timed out; requesting termination"); }
                else if (now - stop_at > std::chrono::seconds(40)) kill(pid, SIGKILL);
            }
            pollfd descriptor{parent.value, POLLIN, 0};
            auto ready = poll(channel_open ? &descriptor : nullptr, channel_open ? 1 : 0, 100);
            if (ready < 0 && errno == EINTR) continue;
            require(ready >= 0, "Restart channel poll failed");
            if (ready > 0) {
                std::array<char, 256> buffer{};
                auto count = recv(parent.value, buffer.data(), buffer.size(), MSG_DONTWAIT | MSG_TRUNC);
                if (count == 0 || (count < 0 && errno != EAGAIN && errno != EINTR)) { channel_open = false; continue; }
                if (count < 0 || count >= ssize_t(buffer.size())) continue;
                const std::string message(buffer.data(), count);
                std::smatch match;
                if (!std::regex_match(message, match,
                                      std::regex("(prepare-stop|commit-stop|prepare|commit) ([a-f0-9]{32})"))) continue;
                const std::string request = match[2];
                const std::string action = match[1];
                if ((action == "prepare" || action == "prepare-stop") && !stopping && !restart && !shutdown) {
                    prepared = request; prepared_action = action; prepared_at = now;
                    const auto response = (action == "prepare-stop" ? "ready-stop " : "ready ") + prepared;
                    if (send(parent.value, response.data(), response.size(), MSG_NOSIGNAL | MSG_DONTWAIT) != ssize_t(response.size())) prepared.clear();
                } else if ((action == "commit" || action == "commit-stop") && request == prepared &&
                           now - prepared_at < std::chrono::seconds(5) && !stopping &&
                           ((action == "commit" && prepared_action == "prepare") ||
                            (action == "commit-stop" && prepared_action == "prepare-stop"))) {
                    restart = action == "commit"; shutdown = action == "commit-stop";
                    prepared.clear(); prepared_action.clear();
                    log(root, restart ? "Acknowledged administration restart" :
                                        "Acknowledged administration shutdown");
                }
            }
        }
    }
    return 0;
}
int deploy(const fs::path& game, const fs::path& archive) {
    require(game.filename() == game_name && fs::is_regular_file(plain(game)) && access(game.c_str(), X_OK) == 0, "Invalid dedicated-server executable");
    const auto root = game.parent_path();
    Lock lock(root);
    require(!game_running(game), "Stop the dedicated server before installation");
    Updater updater(root); updater.recover();
    const auto stage = package_path(root, "Briefcase/Updates/install-" + identifier());
    extract(plain(archive), stage);
    const auto manifest = package_manifest(stage, document(stage / "Package.json").at("frameworkVersion"), digest(game));
    updater.install(stage, manifest);
    updater.cleanup([](const std::string&) {});
    const auto config = package_path(root, "Briefcase/updater.json");
    if (!fs::exists(config)) atomic(config, read(package_path(root, "Briefcase/Core/Updater/updater.example.json")));
    std::printf("Installed Briefcase %s in %s\n", manifest.at("frameworkVersion").get<std::string>().c_str(), root.c_str());
    return 0;
}
}
}
int main(int argc, char** argv) {
    using namespace bc::launcher;
    try {
        fs::path game = executable().parent_path() / game_name;
        fs::path archive;
        std::vector<std::string> arguments;
        bool game_arguments = false;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (!game_arguments && argument == "--") { game_arguments = true; continue; }
            if (!game_arguments && (argument == "--help" || argument == "--version")) {
                std::puts("Briefcase native Linux server launcher " BRIEFCASE_FRAMEWORK_VERSION "\nUsage: Briefcase.ServerLauncher [--server /path/to/DeceiveIncServer-Linux-Shipping] [--install-archive package.zip] [-- game arguments]"); return 0;
            }
            if (!game_arguments && (argument == "--server" || argument == "--install-archive")) {
                require(i + 1 < argc, "Missing launcher option value");
                (argument == "--server" ? game : archive) = plain(argv[++i]); continue;
            }
            arguments.push_back(argument);
        }
        if (!archive.empty()) { require(arguments.empty(), "Installation does not accept game arguments"); return deploy(game, archive); }
        return supervise(game, arguments);
    } catch (const std::exception& error) { std::fprintf(stderr, "Briefcase launcher: %s\n", error.what()); return 78; }
    catch (...) { std::fputs("Briefcase launcher: unexpected failure\n", stderr); return 78; }
}
