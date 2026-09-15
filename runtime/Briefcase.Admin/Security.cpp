#include "Crypto.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <mbedtls/entropy.h>
#include <mbedtls/oid.h>
#include <mbedtls/pk.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/rsa.h>
#include <mbedtls/sha256.h>
#include <mbedtls/threading.h>
#include <mbedtls/x509_crt.h>
#include <mutex>
#include <new>
#include <psa/crypto.h>
#include <stdexcept>
namespace bc::admin {
static void require(bool ok, const char *what) {
    if (!ok)
        throw std::runtime_error(what);
}
void crypto_init() {
    static const bool initialized = [] {
        mbedtls_threading_set_alt(
            [](mbedtls_threading_mutex_t *m) { m->mutex = new (std::nothrow) std::mutex; },
            [](mbedtls_threading_mutex_t *m) {
                delete static_cast<std::mutex *>(m->mutex);
                m->mutex = nullptr;
            },
            [](mbedtls_threading_mutex_t *m) -> int {
                if (!m->mutex)
                    return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
                try {
                    static_cast<std::mutex *>(m->mutex)->lock();
                    return 0;
                } catch (...) {
                    return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
                }
            },
            [](mbedtls_threading_mutex_t *m) -> int {
                if (!m->mutex)
                    return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
                try {
                    static_cast<std::mutex *>(m->mutex)->unlock();
                    return 0;
                } catch (...) {
                    return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
                }
            });
        require(psa_crypto_init() == PSA_SUCCESS, "Unable to initialize cryptography.");
        return true;
    }();
}
int crypto_random(void *, unsigned char *out, size_t size) noexcept {
    // crypto_init is completed by the caller before any Mbed TLS callback.
    return psa_generate_random(out, size) == PSA_SUCCESS ? 0 : MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
}
void erase(std::string &s) noexcept {
    if (!s.empty())
        mbedtls_platform_zeroize(s.data(), s.size());
    s.clear();
}
void erase(Bytes &s) noexcept {
    if (!s.empty())
        mbedtls_platform_zeroize(s.data(), s.size());
    s.clear();
}
std::string hex(std::span<const unsigned char> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (auto b : bytes) {
        out += digits[b >> 4];
        out += digits[b & 15];
    }
    return out;
}
Bytes unhex(std::string_view s) {
    if (s.size() % 2 || s.size() > 131072)
        throw std::runtime_error("Invalid hexadecimal encoding.");
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    Bytes out;
    out.reserve(s.size() / 2);
    for (size_t i = 0; i < s.size(); i += 2) {
        int a = digit(s[i]), b = digit(s[i + 1]);
        if (a < 0 || b < 0)
            throw std::runtime_error("Invalid hexadecimal encoding.");
        out.push_back((a << 4) | b);
    }
    return out;
}

Bytes random_bytes(size_t count) {
    crypto_init();
    Bytes out(count);
    require(crypto_random(nullptr, out.data(), out.size()) == 0, "Random number generator unavailable.");
    return out;
}
std::string digest(std::string_view text) {
    std::array<unsigned char, 32> out{};
    require(
        mbedtls_sha256(reinterpret_cast<const unsigned char *>(text.data()), text.size(), out.data(), 0) == 0,
        "Unable to compute SHA256.");
    return hex(out);
}
Bytes derive(std::string_view password, std::span<const unsigned char> salt, uint64_t iterations) {
    if (password.size() > 256 || salt.size() != 32 || iterations < 600000 || iterations > 2000000)
        throw std::runtime_error("Invalid authentication parameters.");
    Bytes out(32);
    require(mbedtls_pkcs5_pbkdf2_hmac_ext(
                MBEDTLS_MD_SHA256, reinterpret_cast<const unsigned char *>(password.data()), password.size(),
                salt.data(), salt.size(), static_cast<unsigned>(iterations),
                static_cast<uint32_t>(out.size()), out.data()) == 0,
            "Unable to derive password.");
    return out;
}
bool equal(std::span<const unsigned char> a, std::span<const unsigned char> b) noexcept {
    if (a.size() != b.size())
        return false;
    volatile unsigned char different = 0;
    for (size_t i = 0; i < a.size(); ++i)
        different = static_cast<unsigned char>(different | (a[i] ^ b[i]));
    return different == 0;
}

struct Material {
    mbedtls_pk_context key;
    mbedtls_x509_crt cert;
    mbedtls_x509write_cert writer;
    Material() {
        mbedtls_pk_init(&key);
        mbedtls_x509_crt_init(&cert);
        mbedtls_x509write_crt_init(&writer);
    }
    ~Material() {
        mbedtls_x509write_crt_free(&writer);
        mbedtls_x509_crt_free(&cert);
        mbedtls_pk_free(&key);
    }
};
static std::string timestamp(std::chrono::system_clock::time_point point) {
    using namespace std::chrono;
    auto day = floor<days>(point);
    year_month_day date(day);
    hh_mm_ss clock(floor<seconds>(point - day));
    require(date.ok() && int(date.year()) >= 0 && int(date.year()) <= 9999,
            "Certificate date outside supported range.");
    char result[32]{};
    const auto size = std::snprintf(result, sizeof(result), "%04d%02u%02u%02lld%02lld%02lld", int(date.year()),
                  unsigned(date.month()), unsigned(date.day()), static_cast<long long>(clock.hours().count()),
                  static_cast<long long>(clock.minutes().count()),
                  static_cast<long long>(clock.seconds().count()));
    require(size == 14, "Invalid certificate timestamp.");
    return std::string(result, static_cast<size_t>(size));
}
KeyPair create_key_pair() {
    crypto_init();
    Material m;
    require(mbedtls_pk_setup(&m.key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)) == 0,
            "Unable to create key.");
    require(mbedtls_rsa_gen_key(mbedtls_pk_rsa(m.key), crypto_random, nullptr, 3072, 65537) == 0,
            "Unable to generate RSA key.");
    mbedtls_x509write_crt_set_version(&m.writer, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&m.writer, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&m.writer, &m.key);
    mbedtls_x509write_crt_set_issuer_key(&m.writer, &m.key);
    const char *name = "CN=Briefcase Native Administration";
    require(mbedtls_x509write_crt_set_subject_name(&m.writer, name) == 0 &&
                mbedtls_x509write_crt_set_issuer_name(&m.writer, name) == 0,
            "Invalid certificate name.");
    auto serial = random_bytes(16);
    serial[0] &= 0x7f;
    serial[0] |= 1;
    require(mbedtls_x509write_crt_set_serial_raw(&m.writer, serial.data(), serial.size()) == 0,
            "Invalid certificate serial number.");
    auto now = std::chrono::system_clock::now();
    auto from = timestamp(now - std::chrono::minutes(5)), to = timestamp(now + std::chrono::days(365));
    require(mbedtls_x509write_crt_set_validity(&m.writer, from.c_str(), to.c_str()) == 0,
            "Invalid certificate validity.");
    require(mbedtls_x509write_crt_set_basic_constraints(&m.writer, 0, -1) == 0 &&
                mbedtls_x509write_crt_set_key_usage(&m.writer, MBEDTLS_X509_KU_DIGITAL_SIGNATURE) == 0,
            "Invalid certificate usages.");
    mbedtls_asn1_sequence eku{};
    eku.buf.tag = MBEDTLS_ASN1_OID;
    eku.buf.p = reinterpret_cast<unsigned char *>(const_cast<char *>(MBEDTLS_OID_SERVER_AUTH));
    eku.buf.len = MBEDTLS_OID_SIZE(MBEDTLS_OID_SERVER_AUTH);
    require(mbedtls_x509write_crt_set_ext_key_usage(&m.writer, &eku) == 0, "Invalid TLS server usage.");
    Bytes cert(8192), key(8192);
    struct Wipe {
        Bytes &b;
        ~Wipe() { erase(b); }
    } wipe{key};
    int certSize = mbedtls_x509write_crt_der(&m.writer, cert.data(), cert.size(), crypto_random, nullptr);
    int keySize = mbedtls_pk_write_key_der(&m.key, key.data(), key.size());
    require(certSize > 0 && keySize > 0, "Unable to export identity.");
    KeyPair out;
    out.certificate = hex(std::span(cert).last(certSize));
    out.fingerprint =
        digest({reinterpret_cast<char *>(cert.data() + cert.size() - certSize), size_t(certSize)});
    out.private_key.assign(key.end() - keySize, key.end());
    return out;
}
void validate_key_pair(std::string_view certificate, std::span<const unsigned char> key,
                       std::string_view fingerprint) {
    crypto_init();
    Material m;
    auto der = unhex(certificate);
    require(digest({reinterpret_cast<char *>(der.data()), der.size()}) == fingerprint,
            "Invalid fingerprint.");
    require(mbedtls_x509_crt_parse_der(&m.cert, der.data(), der.size()) == 0 &&
                !mbedtls_x509_time_is_past(&m.cert.valid_to) &&
                !mbedtls_x509_time_is_future(&m.cert.valid_from),
            "Invalid or expired certificate.");
    require(mbedtls_pk_parse_key(&m.key, key.data(), key.size(), nullptr, 0, crypto_random, nullptr) == 0 &&
                mbedtls_pk_check_pair(&m.cert.pk, &m.key, crypto_random, nullptr) == 0,
            "Key and certificate do not match.");
}
} // namespace bc::admin
