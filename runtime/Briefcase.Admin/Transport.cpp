#include "Transport.hpp"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/threading.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>
#include <stdexcept>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>
namespace bc::admin {
static void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
static void tls_check(int code, const char *what) {
    if (code) {
        char error[200]{};
        mbedtls_strerror(code, error, sizeof(error));
        throw std::runtime_error(std::format("{} : {}", what, error));
    }
}
static void winsock() {
    static const bool initialized = [] {
        WSADATA w{};
        check(WSAStartup(MAKEWORD(2, 2), &w) == 0, "Winsock unavailable.");
        return true;
    }();
}
struct Socket {
    SOCKET s{INVALID_SOCKET};
    ~Socket() {
        if (s != INVALID_SOCKET)
            closesocket(s);
    }
};
static void nonblocking(SOCKET s) {
    u_long on = 1;
    check(ioctlsocket(s, FIONBIO, &on) == 0, "Unable to set socket mode.");
    BOOL yes = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char *>(&yes), sizeof(yes));
}
static void wait_socket(SOCKET s, bool writing, const std::atomic<bool> &stop, Clock::time_point deadline) {
    for (;;) {
        if (stop.load())
            throw std::runtime_error("Connection cancelled.");
        if (Clock::now() >= deadline)
            throw std::runtime_error("Connection timed out.");
        fd_set f;
        FD_ZERO(&f);
        FD_SET(s, &f);
        timeval t{0, 100000};
        int r = select(0, writing ? nullptr : &f, writing ? &f : nullptr, nullptr, &t);
        check(r != SOCKET_ERROR, "Network error.");
        if (r > 0)
            return;
    }
}
struct Credentials::Impl {
    IdentityData identity;
    bool server{};
};
Credentials::Credentials() : impl(std::make_shared<Impl>()) {
    crypto_init();
}
Credentials::Credentials(const IdentityData &data) : impl(std::make_shared<Impl>()) {
    crypto_init();
    validate_identity(data);
    impl->identity = data;
    impl->server = true;
}
struct Stream::Impl {
    Socket socket;
    std::shared_ptr<Credentials::Impl> credentials;
    mbedtls_ssl_context ssl{};
    mbedtls_ssl_config config{};
    mbedtls_x509_crt cert{};
    mbedtls_pk_context key{};
    const std::atomic<bool> *stop{};
    std::string address, tls;
    Bytes expected_pin;
    Impl() {
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&config);
        mbedtls_x509_crt_init(&cert);
        mbedtls_pk_init(&key);
    }
    ~Impl() {
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&config);
        mbedtls_x509_crt_free(&cert);
        mbedtls_pk_free(&key);
    }
    static int send_bytes(void *context, const unsigned char *buffer, size_t length) noexcept {
        auto *p = static_cast<Impl *>(context);
        int n = ::send(p->socket.s, reinterpret_cast<const char *>(buffer), int(length), 0);
        if (n == SOCKET_ERROR)
            return WSAGetLastError() == WSAEWOULDBLOCK ? MBEDTLS_ERR_SSL_WANT_WRITE
                                                       : MBEDTLS_ERR_NET_SEND_FAILED;
        return n;
    }
    static int recv_bytes(void *context, unsigned char *buffer, size_t length) noexcept {
        auto *p = static_cast<Impl *>(context);
        int n = ::recv(p->socket.s, reinterpret_cast<char *>(buffer), int(length), 0);
        if (n == SOCKET_ERROR)
            return WSAGetLastError() == WSAEWOULDBLOCK ? MBEDTLS_ERR_SSL_WANT_READ
                                                       : MBEDTLS_ERR_NET_RECV_FAILED;
        return n;
    }
    static int verify(void *context, mbedtls_x509_crt *certificate, int depth, uint32_t *flags) noexcept {
        try {
            auto *p = static_cast<Impl *>(context);
            if (depth != 0)
                return 0;
            auto actual = unhex(digest({reinterpret_cast<char *>(certificate->raw.p), certificate->raw.len}));
            if (!equal(p->expected_pin, actual)) {
                *flags |= MBEDTLS_X509_BADCERT_NOT_TRUSTED;
                return MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
            }
            // Exact leaf pin is the trust anchor; validity, usage and algorithm checks remain enforced.
            *flags &= ~uint32_t(MBEDTLS_X509_BADCERT_NOT_TRUSTED | MBEDTLS_X509_BADCERT_CN_MISMATCH);
            return 0;
        } catch (...) {
            return MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
        }
    }
    void pending(int result, Clock::time_point end) {
        if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE)
            wait_socket(socket.s, result == MBEDTLS_ERR_SSL_WANT_WRITE, *stop, end);
        else
            tls_check(result, "TLS connection refused");
    }
    void handshake(bool server, const std::wstring &hostname = {}) {
        tls_check(mbedtls_ssl_config_defaults(&config, server ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT,
                                              MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT),
                  "Unable to configure TLS");
        mbedtls_ssl_conf_min_tls_version(&config, MBEDTLS_SSL_VERSION_TLS1_2);
        mbedtls_ssl_conf_max_tls_version(&config, MBEDTLS_SSL_VERSION_TLS1_3);
        static const int suites[]{MBEDTLS_TLS1_3_AES_256_GCM_SHA384,
                                  MBEDTLS_TLS1_3_AES_128_GCM_SHA256,
                                  MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
                                  MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
                                  MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
                                  MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
                                  0};
        mbedtls_ssl_conf_ciphersuites(&config, suites);
        mbedtls_ssl_conf_rng(&config, crypto_random, nullptr);
        mbedtls_ssl_conf_session_tickets(&config, MBEDTLS_SSL_SESSION_TICKETS_DISABLED);
        if (server) {
            const auto &data = credentials->identity;
            auto der = unhex(data.certificate), plain = identity_private_key(data);
            struct Wipe {
                Bytes &b;
                ~Wipe() { erase(b); }
            } wipe{plain};
            tls_check(mbedtls_x509_crt_parse_der(&cert, der.data(), der.size()), "Invalid TLS certificate");
            tls_check(
                mbedtls_pk_parse_key(&key, plain.data(), plain.size(), nullptr, 0, crypto_random, nullptr),
                "Invalid TLS key");
            tls_check(mbedtls_ssl_conf_own_cert(&config, &cert, &key), "Incompatible TLS certificate");
            mbedtls_ssl_conf_authmode(&config,
                                      MBEDTLS_SSL_VERIFY_NONE); // Password authentication follows TLS.
        } else {
            check(expected_pin.size() == 32, "Server SHA256 fingerprint required.");
            mbedtls_ssl_conf_authmode(
                &config,
                MBEDTLS_SSL_VERIFY_OPTIONAL); // Exact leaf pin is enforced below, before application data.
            mbedtls_ssl_conf_verify(&config, verify, this);
        }
        tls_check(mbedtls_ssl_setup(&ssl, &config), "Unable to create TLS context");
        if (!server) {
            std::string name(hostname.begin(), hostname.end());
            tls_check(mbedtls_ssl_set_hostname(&ssl, name.c_str()), "Invalid TLS name");
        }
        mbedtls_ssl_set_bio(&ssl, this, send_bytes, recv_bytes, nullptr);
        const auto end = Clock::now() + std::chrono::seconds(10);
        for (;;) {
            int code = mbedtls_ssl_handshake(&ssl);
            if (code == 0)
                break;
            pending(code, end);
        }
        tls = mbedtls_ssl_get_version(&ssl);
        if (!server) {
            const auto *leaf = mbedtls_ssl_get_peer_cert(&ssl);
            check(leaf != nullptr, "Missing server certificate.");
            auto actual = unhex(digest({reinterpret_cast<char *>(leaf->raw.p), leaf->raw.len}));
            check(equal(expected_pin, actual) && mbedtls_ssl_get_verify_result(&ssl) == 0,
                  "Server identity refused. No password was sent.");
        }
    }
    void write(std::span<const unsigned char> bytes, Clock::time_point end) {
        while (!bytes.empty()) {
            check(!stop->load(), "Connection cancelled.");
            int n = mbedtls_ssl_write(&ssl, bytes.data(), std::min(bytes.size(), size_t(16384)));
            if (n > 0)
                bytes = bytes.subspan(n);
            else
                pending(n == 0 ? MBEDTLS_ERR_NET_SEND_FAILED : n, end);
        }
    }
    void read(std::span<unsigned char> bytes, Clock::time_point end) {
        while (!bytes.empty()) {
            check(!stop->load(), "Connection cancelled.");
            int n = mbedtls_ssl_read(&ssl, bytes.data(), bytes.size());
            if (n > 0)
                bytes = bytes.subspan(n);
            else
                pending(n == 0 ? MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY : n, end);
        }
    }
};
Stream::Stream(std::unique_ptr<Impl> i) : p(std::move(i)) {}
Stream::~Stream() = default;
Stream::Stream(Stream &&) noexcept = default;
Stream &Stream::operator=(Stream &&) noexcept = default;
static std::pair<std::wstring, std::wstring> split_endpoint(const std::string &s) {
    check(!s.empty() && s.size() <= 255, "Invalid administration address.");
    std::string host, port;
    if (s.front() == '[') {
        auto e = s.find(']');
        check(e != s.npos && e + 1 < s.size() && s[e + 1] == ':', "Invalid IPv6 address.");
        host = s.substr(1, e - 1);
        port = s.substr(e + 2);
    } else {
        auto colon = s.find(':');
        check(colon != s.npos && s.find(':', colon + 1) == s.npos, "Expected address: host:port.");
        host = s.substr(0, colon);
        port = s.substr(colon + 1);
    }
    check(!host.empty() && !port.empty() && port.size() <= 5 &&
              std::all_of(port.begin(), port.end(), [](char c) { return c >= '0' && c <= '9'; }),
          "Invalid port.");
    check(std::stoul(port) > 0 && std::stoul(port) <= 65535, "Invalid port.");
    check(std::all_of(host.begin(), host.end(),
                      [](unsigned char c) { return c > 32 && c < 127 && c != '/' && c != '\\'; }),
          "Invalid host.");
    return {{host.begin(), host.end()}, {port.begin(), port.end()}};
}
Stream Stream::connect(const Credentials &c, const std::string &endpoint, const std::string &pin,
                       const std::atomic<bool> &stop) {
    check(unhex(pin).size() == 32, "Server SHA256 fingerprint required.");
    winsock();
    auto [host, port] = split_endpoint(endpoint);
    struct Resolution {
        ADDRINFOEXW *result{};
        HANDLE event{}, cancel{};
        OVERLAPPED overlapped{};
        ~Resolution() {
            if (result)
                FreeAddrInfoExW(result);
            if (event)
                CloseHandle(event);
        }
    } r;
    r.event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    check(r.event != nullptr, "DNS resolution unavailable.");
    r.overlapped.hEvent = r.event;
    ADDRINFOEXW hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    auto status = GetAddrInfoExW(host.c_str(), port.c_str(), NS_DNS, nullptr, &hints, &r.result, nullptr,
                                 &r.overlapped, nullptr, &r.cancel);
    if (status == WSA_IO_PENDING) {
        const auto end = Clock::now() + std::chrono::seconds(5);
        while (WaitForSingleObject(r.event, 100) == WAIT_TIMEOUT) {
            if (stop || Clock::now() >= end) {
                GetAddrInfoExCancel(&r.cancel);
                WaitForSingleObject(r.event, INFINITE);
                throw std::runtime_error("DNS resolution cancelled or timed out.");
            }
        }
        status = GetAddrInfoExOverlappedResult(&r.overlapped);
    }
    check(status == 0, "Host not found.");
    auto p = std::make_unique<Impl>();
    p->credentials = c.impl;
    p->stop = &stop;
    p->address = endpoint;
    const auto end = Clock::now() + std::chrono::seconds(5);
    for (auto *a = r.result; a; a = a->ai_next) {
        Socket sock;
        sock.s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (sock.s == INVALID_SOCKET)
            continue;
        nonblocking(sock.s);
        int code = ::connect(sock.s, a->ai_addr, int(a->ai_addrlen));
        if (code == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
            continue;
        if (code == SOCKET_ERROR) {
            try {
                wait_socket(sock.s, true, stop, end);
            } catch (...) {
                if (!a->ai_next || stop)
                    throw;
                continue;
            }
            int err = 0, len = sizeof(err);
            if (getsockopt(sock.s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&err), &len) != 0 || err)
                continue;
        }
        p->socket.s = sock.s;
        sock.s = INVALID_SOCKET;
        break;
    }
    check(p->socket.s != INVALID_SOCKET, "Administration server unreachable.");
    p->expected_pin = unhex(pin);
    p->handshake(false, host);
    return Stream(std::move(p));
}
void Stream::send(std::string_view s, size_t limit) {
    check(!s.empty() && s.size() <= limit, "Message is too large.");
    uint32_t length = htonl(uint32_t(s.size()));
    const auto end = Clock::now() + std::chrono::seconds(10);
    p->write({reinterpret_cast<unsigned char *>(&length), 4}, end);
    p->write({reinterpret_cast<const unsigned char *>(s.data()), s.size()}, end);
}
std::string Stream::receive(size_t limit, unsigned idle_seconds) {
    uint32_t length = 0;
    p->read({reinterpret_cast<unsigned char *>(&length), 4},
            Clock::now() + std::chrono::seconds(idle_seconds));
    length = ntohl(length);
    check(length > 0 && length <= limit, "Message length refused.");
    std::string out(length, 0);
    try {
        p->read({reinterpret_cast<unsigned char *>(out.data()), length},
                Clock::now() + std::chrono::seconds(10));
    } catch (...) {
        erase(out);
        throw;
    }
    return out;
}
const std::string &Stream::peer() const {
    return p->address;
}
const std::string &Stream::protocol() const {
    return p->tls;
}
struct Listener::Impl {
    Socket socket;
    const std::atomic<bool> *stop{};
    uint16_t port{};
};
Listener::Listener(const std::string &address, uint16_t port, const std::atomic<bool> &stop)
    : p(std::make_unique<Impl>()) {
    winsock();
    p->stop = &stop;
    ADDRINFOA hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    ADDRINFOA *result{};
    check(getaddrinfo(address.c_str(), std::to_string(port).c_str(), &hints, &result) == 0,
          "A numeric listen address is required.");
    struct Free {
        ADDRINFOA *p;
        ~Free() { freeaddrinfo(p); }
    } free{result};
    p->socket.s = socket(result->ai_family, SOCK_STREAM, IPPROTO_TCP);
    check(p->socket.s != INVALID_SOCKET, "Unable to create socket.");
    BOOL exclusive = TRUE;
    check(setsockopt(p->socket.s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<char *>(&exclusive),
                     sizeof(exclusive)) == 0,
          "Unable to reserve the port.");
    check(bind(p->socket.s, result->ai_addr, int(result->ai_addrlen)) == 0 && listen(p->socket.s, 4) == 0,
          "Administration port unavailable.");
    nonblocking(p->socket.s);
    sockaddr_storage actual{};
    int size = sizeof(actual);
    check(getsockname(p->socket.s, reinterpret_cast<sockaddr *>(&actual), &size) == 0, "Port not found.");
    p->port = ntohs(actual.ss_family == AF_INET ? reinterpret_cast<sockaddr_in *>(&actual)->sin_port
                                                : reinterpret_cast<sockaddr_in6 *>(&actual)->sin6_port);
}
Listener::~Listener() = default;
uint16_t Listener::port() const {
    return p->port;
}
std::unique_ptr<Stream> Listener::accept(const Credentials &credentials) {
    sockaddr_storage addr{};
    int size = sizeof(addr);
    auto s = ::accept(p->socket.s, reinterpret_cast<sockaddr *>(&addr), &size);
    if (s == INVALID_SOCKET) {
        if (WSAGetLastError() == WSAEWOULDBLOCK)
            return {};
        throw std::runtime_error("Unable to accept network connection.");
    }
    auto stream = std::make_unique<Stream::Impl>();
    stream->socket.s = s;
    stream->credentials = credentials.impl;
    stream->stop = p->stop;
    char name[NI_MAXHOST]{};
    getnameinfo(reinterpret_cast<sockaddr *>(&addr), size, name, sizeof(name), nullptr, 0, NI_NUMERICHOST);
    stream->address = name;
    nonblocking(s);
    stream->handshake(true);
    return std::unique_ptr<Stream>(new Stream(std::move(stream)));
}
} // namespace bc::admin
