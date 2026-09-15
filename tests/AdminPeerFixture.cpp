#include "../runtime/Briefcase.Admin/Service.hpp"
#include <iostream>
using namespace bc::admin;
int main() {
    try {
        const auto parent = fs::current_path();
        const auto root = parent / ("admin-interop-" + hex(random_bytes(8)));
        fs::create_directory(root);
        struct Cleanup {
            fs::path root, parent;
            ~Cleanup() {
                std::error_code e;
                if (root.parent_path() == parent && root.filename().string().starts_with("admin-interop-"))
                    fs::remove_all(root, e);
            }
        } cleanup{root, parent};
        auto settings = provision(root, "127.0.0.1", 50002, "127.0.0.1:50002", "Fixture-password-only-2026");
        settings.port = 0;
        auto configs = std::make_shared<ConfigStore>();
        Server server(
            settings, configs, [] { return Json{{"framework", "fixture"}, {"ready", true}}; },
            [] { return Json::array(); });
        server.start();
        std::cout << Json{{"endpoint", "127.0.0.1:" + std::to_string(server.port())},
                          {"fingerprint", settings.identity.fingerprint}}
                         .dump()
                  << std::endl;
        std::string line;
        std::getline(std::cin, line);
        server.stop();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
