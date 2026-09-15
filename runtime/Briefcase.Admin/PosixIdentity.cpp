#include "Transport.hpp"
#include <stdexcept>
namespace bc::admin {
// This representation is stored only in a mode-0600 server configuration file.
// It is explicit so a Windows DPAPI blob is never mistaken for a DER key.
static constexpr std::string_view prefix = "der-v1:";
IdentityData create_identity() {
    auto pair = create_key_pair();
    auto key = std::string(prefix) + hex(pair.private_key);
    erase(pair.private_key);
    return {pair.certificate, std::move(key), pair.fingerprint};
}
Bytes identity_private_key(const IdentityData& identity) {
    if (!identity.protected_key.starts_with(prefix))
        throw std::runtime_error("TLS identity uses another OS storage format; provision a Linux identity.");
    return unhex(std::string_view(identity.protected_key).substr(prefix.size()));
}
void validate_identity(const IdentityData& identity) {
    auto key = identity_private_key(identity);
    struct Wipe { Bytes& key; ~Wipe() { erase(key); } } wipe{key};
    validate_key_pair(identity.certificate, key, identity.fingerprint);
}
}
