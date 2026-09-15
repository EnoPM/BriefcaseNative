#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace bc::admin {
using Bytes = std::vector<unsigned char>;
void crypto_init();
int crypto_random(void *, unsigned char *, size_t) noexcept;
std::string hex(std::span<const unsigned char>);
Bytes unhex(std::string_view);
Bytes random_bytes(size_t);
std::string digest(std::string_view);
Bytes derive(std::string_view, std::span<const unsigned char>, uint64_t iterations = 600000);
bool equal(std::span<const unsigned char>, std::span<const unsigned char>) noexcept;
void erase(std::string &) noexcept;
void erase(Bytes &) noexcept;
struct KeyPair {
    std::string certificate, fingerprint;
    Bytes private_key; // In memory only; the platform identity store encrypts it before persistence.
    ~KeyPair() { erase(private_key); }
    KeyPair() = default;
    KeyPair(const KeyPair &) = default;
    KeyPair(KeyPair &&) noexcept = default;
};
KeyPair create_key_pair();
void validate_key_pair(std::string_view certificate, std::span<const unsigned char> private_key,
                       std::string_view fingerprint);
} // namespace bc::admin
