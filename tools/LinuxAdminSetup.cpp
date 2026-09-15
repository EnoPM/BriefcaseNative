#include "../runtime/Briefcase.Admin/Service.hpp"
#include "../runtime/Briefcase.NativeHost/Startup.hpp"
#include <charconv>
#include <iostream>
using namespace bc::admin;
int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "Usage: Briefcase.AdminSetup <existing-Briefcase-directory> <listen-address> <port> <public-endpoint>\n"
                     "Generates a password in Admin/server.json (mode 600); never prints the password.\n";
        return 2;
    }
    try {
        const auto root = fs::absolute(argv[1]).lexically_normal();
        bc::assert_plain_path(root);
        if (!fs::is_directory(root)) throw std::runtime_error("Briefcase directory does not exist.");
        unsigned port{};
        const std::string port_text = argv[3];
        const auto result = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
        if (result.ec != std::errc{} || result.ptr != port_text.data() + port_text.size() || !port || port > 65535)
            throw std::runtime_error("Invalid administration port.");
        auto password = hex(random_bytes(24));
        struct Wipe { std::string& value; ~Wipe() { erase(value); } } wipe{password};
        provision(root, argv[2], uint16_t(port), argv[4], password);
        std::cout << "Administration configured. Password: Admin/server.json; public identity: Admin/pairing.json\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
