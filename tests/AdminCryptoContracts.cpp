#include "../runtime/Briefcase.Admin/Crypto.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <set>
using namespace bc::admin;
static unsigned checks;
static void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
static std::string read(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("Missing synthetic cross-platform fixture.");
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
int main(int argc, char **argv) {
    try {
        auto started = std::chrono::steady_clock::now();
        check(digest("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "SHA-256 known vector");
        check(digest("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              "SHA-256 empty vector");
        Bytes salt(32);
        for (unsigned i = 0; i < 32; i++)
            salt[i] = static_cast<unsigned char>(i);
        check(hex(derive("Portable-fixture-only", salt)) ==
                  "3a27aae781112d3124404c923b39e334c5854bebcd6cfbd9ad2e84e23b117451",
              "PBKDF2 independent Python vector");
        std::vector<std::future<Bytes>> futures;
        for (int i = 0; i < 8; i++)
            futures.push_back(std::async(std::launch::async, [] { return random_bytes(32); }));
        std::set<std::string> randoms;
        for (auto &job : futures)
            randoms.insert(hex(job.get()));
        check(randoms.size() == 8, "Concurrent random generation");
        check(equal(salt, salt) && !equal(salt, Bytes(32)), "Constant-time comparison result");
        auto pair = create_key_pair();
        validate_key_pair(pair.certificate, pair.private_key, pair.fingerprint);
        check(pair.private_key.size() > 1000, "Generated RSA 3072 identity validates");
        bool rejected = false;
        try {
            validate_key_pair(pair.certificate, Bytes(32), pair.fingerprint);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "Invalid private key rejected");
        rejected = false;
        try {
            validate_key_pair(pair.certificate, pair.private_key, std::string(64, '0'));
        } catch (...) {
            rejected = true;
        }
        check(rejected, "Certificate digest mismatch rejected");
        auto text = std::string("fixture-secret");
        erase(text);
        check(text.empty(), "String cleared");
        auto bytes = random_bytes(8);
        erase(bytes);
        check(bytes.empty(), "Bytes cleared");
        // These are randomly generated disposable TEST identities, never a deployed server identity.
        if (argc == 3) {
            auto directory = std::filesystem::absolute(argv[2]);
            if (std::string_view(argv[1]) == "--export-fixture") {
                std::filesystem::create_directories(directory);
                std::ofstream(directory / "test-cert.hex") << pair.certificate;
                std::ofstream(directory / "test-key.hex") << hex(pair.private_key);
                std::ofstream(directory / "test-fingerprint.hex") << pair.fingerprint;
            } else if (std::string_view(argv[1]) == "--verify-fixture") {
                auto key = unhex(read(directory / "test-key.hex"));
                validate_key_pair(read(directory / "test-cert.hex"), key,
                                  read(directory / "test-fingerprint.hex"));
                erase(key);
                check(true, "Identity from other operating system validated");
            } else
                throw std::runtime_error("Unknown fixture option.");
        }
        std::cout
            << "PASS " << checks << " portable crypto checks; "
            << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count()
            << " ms\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
