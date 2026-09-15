#include "../runtime/Briefcase.Client.Servers/ServerDirectory.hpp"
#include "../runtime/Briefcase.UnrealBackend/ClientConnection.hpp"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace bc::servers;
static unsigned checks;
static void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> static void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (...) {
        rejected = true;
    }
    check(rejected, "invalid input rejected");
}
int main() {
    try {
        check(normalize_endpoint(" LOCALHOST:07777 ") == "localhost:7777", "canonical DNS and port");
        check(normalize_endpoint("127.0.0.1:7777") == "127.0.0.1:7777", "IPv4");
        check(normalize_endpoint("[0:0:0:0:0:0:0:1]:7777") == "[::1]:7777", "canonical IPv6");
        for (auto input :
             {"", "localhost", "localhost:0", "localhost:65536", "localhost:-1", "localhost:77;quit",
              "host:7777?Password=x", "https://host:7777", "127.0.0.999:7777", "[xyz]:7777", "::1:7777",
              "bad host:7777", "-host:7777", "a..b:7777", "host:7\n7"})
            rejects([&] { normalize_endpoint(input); });
        check(normalize_name("  Serveur été  ") == "Serveur été", "UTF-8 names preserved");
        rejects([] { normalize_name(std::string(96, 'x')); });
        rejects([] { normalize_name(" \n "); });
        rejects([] { normalize_name("A\nB"); });
        rejects([] { normalize_name(std::string("\xff")); });
        namespace fs = std::filesystem;
        const auto root = fs::current_path() / ("server-contracts-" + std::to_string(GetCurrentProcessId()));
        check(!fs::exists(root), "unique fixture directory");
        fs::create_directory(root);
        const auto file = root / "servers.json";
        Directory directory;
        directory.load(file);
        check(directory.snapshot().writable && directory.snapshot().entries.empty(),
              "missing file starts empty");
        check(directory.add("Local", "LOCALHOST:7777") == BC_OK, "enqueue add");
        check(!fs::exists(file) && directory.snapshot().pending, "no disk write from UI call");
        check(directory.add("Other", "host:7777") == BC_NOT_READY, "serialize concurrent UI requests");
        check(!directory.process_one().empty(), "worker persists");
        const auto first = directory.snapshot().entries.front();
        check(first.endpoint == "localhost:7777" && !directory.snapshot().pending, "commit canonical entry");
        check(directory.add("Duplicate", "localhost:07777") == BC_INVALID_ARGUMENT,
              "normalized duplicates rejected");
        Directory reload;
        reload.load(file);
        check(reload.find(first.id)->name == "Local", "reload stable ID and name");
        check(reload.remove(98765) == BC_NOT_FOUND, "unknown removal rejected");
        check(reload.add("Remote", "example.org:7778") == BC_OK, "second add");
        reload.process_one();
        check(reload.remove(first.id) == BC_OK, "queue deletion");
        reload.process_one();
        Directory after;
        after.load(file);
        check(after.snapshot().entries.size() == 1 && !after.find(first.id), "deletion persisted");
        const auto current = after.snapshot().entries.front();
        HANDLE hold =
            CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        check(hold != INVALID_HANDLE_VALUE, "hold existing destination");
        check(after.remove(current.id) == BC_OK, "queue replacement failure");
        after.process_one();
        CloseHandle(hold);
        check(after.find(current.id).has_value() && !after.snapshot().pending,
              "failed write does not lose entries");
        Directory intact;
        intact.load(file);
        check(intact.find(current.id).has_value(), "old file preserved on failure");
        check(after.remove(current.id) == BC_OK, "retry after failure");
        after.process_one();
        check(after.snapshot().entries.empty(), "retry saves successfully");
        {
            std::ofstream bad(file);
            bad << "{malformed";
        }
        Directory broken;
        broken.load(file);
        check(!broken.snapshot().writable && broken.add("x", "localhost:7777") == BC_DENIED,
              "malformed file read-only");
        check(fs::file_size(file) == 10, "malformed original retained");
        Directory bounded;
        bounded.load(root / "bounded.json");
        for (unsigned i = 0; i < 64; ++i) {
            check(bounded.add("Server " + std::to_string(i), "localhost:" + std::to_string(7000 + i)) ==
                      BC_OK,
                  "add within limit");
            bounded.process_one();
        }
        check(bounded.add("Extra", "localhost:8000") == BC_LIMIT, "bounded directory");
        std::array<bc::ConnectParameter, 2> p{
            {{L"IPPort", 0, 16, 1, true, true}, {L"Password", 16, 16, 1, true, true}}};
        check(bc::direct_connect_contract(32, p), "generated DirectConnect signature accepted");
        check(!bc::direct_connect_contract(24, p), "wrong parameter buffer rejected");
        p[1].offset = 8;
        check(!bc::direct_connect_contract(32, p), "overlapping strings rejected");
        p[1].offset = 16;
        p[1].input = false;
        check(!bc::direct_connect_contract(32, p), "output string rejected");
        p[1].input = true;
        p[0].name = L"Command";
        check(!bc::direct_connect_contract(32, p), "different function contract rejected");
        // Remove only the unique fixture subtree this process just created.
        check(root.parent_path() == fs::current_path(), "fixture cleanup stays in build");
        fs::remove_all(root);
        std::cout << "PASS " << checks << " server directory and direct-connect contracts\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
