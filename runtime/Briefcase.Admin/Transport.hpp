#pragma once
#include "Crypto.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace bc::admin {
using Clock = std::chrono::steady_clock;
struct IdentityData {
    std::string certificate, protected_key, fingerprint;
};
IdentityData create_identity();
Bytes identity_private_key(const IdentityData &);
void validate_identity(const IdentityData &);
class Credentials {
  public:
    struct Impl;
    std::shared_ptr<Impl> impl;
    Credentials(); // Client: no implicit Windows account/certificate credentials.
    explicit Credentials(const IdentityData &);
};
class Stream {
    struct Impl;
    std::unique_ptr<Impl> p;
    explicit Stream(std::unique_ptr<Impl>);
    friend class Listener;

  public:
    ~Stream();
    Stream(Stream &&) noexcept;
    Stream &operator=(Stream &&) noexcept;
    static Stream connect(const Credentials &, const std::string &endpoint, const std::string &pin,
                          const std::atomic<bool> &stop);
    void send(std::string_view, size_t limit = 1048576);
    std::string receive(size_t limit = 65536, unsigned idle_seconds = 120);
    const std::string &peer() const;
    const std::string &protocol() const;
};
class Listener {
    struct Impl;
    std::unique_ptr<Impl> p;

  public:
    Listener(const std::string &numeric_address, uint16_t port, const std::atomic<bool> &stop);
    ~Listener();
    uint16_t port() const;
    std::unique_ptr<Stream> accept(const Credentials &); // nonblocking accept; bounded handshake
};
} // namespace bc::admin
