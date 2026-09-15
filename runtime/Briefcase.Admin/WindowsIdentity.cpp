// Existing Windows account-bound protection is deliberately preserved.
// A future portable storage migration requires an explicit secret-storage choice.
#include "Transport.hpp"
#include <Windows.h>
#include <stdexcept>
#include <wincrypt.h>
namespace bc::admin {
struct LocalBlob {
    DATA_BLOB value{};
    ~LocalBlob() {
        if (value.pbData) {
            SecureZeroMemory(value.pbData, value.cbData);
            LocalFree(value.pbData);
        }
    }
};
IdentityData create_identity() {
    auto pair = create_key_pair();
    DATA_BLOB input{DWORD(pair.private_key.size()), pair.private_key.data()};
    LocalBlob encrypted;
    if (!CryptProtectData(&input, L"Briefcase Administration TLS", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &encrypted.value))
        throw std::runtime_error("Unable to protect data with DPAPI.");
    return {pair.certificate, hex({encrypted.value.pbData, encrypted.value.cbData}), pair.fingerprint};
}
Bytes identity_private_key(const IdentityData &identity) {
    auto encrypted = unhex(identity.protected_key);
    DATA_BLOB input{DWORD(encrypted.size()), encrypted.data()};
    LocalBlob plain;
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                            &plain.value))
        throw std::runtime_error("TLS key inaccessible: use the Windows account that configured it.");
    return {plain.value.pbData, plain.value.pbData + plain.value.cbData};
}
void validate_identity(const IdentityData &identity) {
    auto key = identity_private_key(identity);
    struct Wipe {
        Bytes &b;
        ~Wipe() { erase(b); }
    } wipe{key};
    validate_key_pair(identity.certificate, key, identity.fingerprint);
}
} // namespace bc::admin
