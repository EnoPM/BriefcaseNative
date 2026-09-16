// Test executable only: failure injection is never shipped in the launcher.
#include "LinuxUpdate.hpp"
#include <cstdio>
#include <unistd.h>
using namespace bc::launcher;
int main(int argc, char** argv) {
    try {
        require(argc >= 3, "Missing test command");
        const std::string command = argv[1];
        if (command == "path") std::puts(package_path(argv[2], argv[3]).c_str());
        else if (command == "document") std::puts(document(argv[2]).dump().c_str());
        else if (command == "url") allowed_url(argv[2]);
        else if (command == "select") {
            const auto asset = select_asset(document(argv[2]), argv[3], argv[4]);
            std::puts(asset ? asset->dump().c_str() : "null");
        } else if (command == "extract") extract(argv[2], argv[3]);
        else if (command == "manifest") std::puts(package_manifest(argv[2], argv[3], argv[4]).dump().c_str());
        else if (command == "install") {
            Updater updater(argv[2]);
            updater.install(argv[3], document(fs::path(argv[3]) / "Package.json"), [&](size_t index) {
                if (argc > 4 && index == 1) {
                    if (std::string(argv[4]) == "crash") _exit(99);
                    throw std::runtime_error("Simulated write failure");
                }
            });
        } else if (command == "recover") Updater(argv[2]).recover();
        else if (command == "update") {
            Updater updater(argv[2], [&](const std::string& url, const fs::path& path, int, uint64_t maximum) {
                require(argc == 5, "Offline download fixture");
                atomic(path, read(url.ends_with("latest") ? argv[3] : argv[4], maximum));
            });
            std::puts(updater.update([](const std::string&) {}).c_str());
        } else if (command == "download") download(argv[2], argv[3], 10, max_archive);
        else throw std::runtime_error("Unknown test command");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 78; }
}
