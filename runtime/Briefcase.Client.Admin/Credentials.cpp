#include "Credentials.hpp"
#include "../Briefcase.NativeHost/Configuration.hpp"
#include "../Briefcase.NativeHost/Startup.hpp"
#include <Windows.h>
#include <wincrypt.h>
namespace bc::admin {
Json PasswordStore::load() {
    if (!fs::exists(file))
        return Json::object();
    auto j = strict_json(read_file(file, 262144), 262144);
    if (!j.is_object() || j.size() > 64)
        throw Error("credentials", "Invalid local credential store.");
    return j;
}
static std::string binding(const std::string &game, const std::string &endpoint, const std::string &pin) {
    return Json::array({game, endpoint, pin}).dump();
}
bool PasswordStore::contains(const std::string &game, const std::string &endpoint, const std::string &pin) {
    auto j = load();
    return j.contains(game) && j[game].is_object() && j[game].value("endpoint", "") == endpoint &&
           j[game].value("fingerprint", "") == pin && j[game].contains("protectedPassword");
}
std::string PasswordStore::get(const std::string &game, const std::string &endpoint, const std::string &pin) {
    auto j = load();
    if (!j.contains(game) || j[game].value("endpoint", "") != endpoint ||
        j[game].value("fingerprint", "") != pin)
        throw Error("credentials", "No password saved for this identity.");
    auto encrypted = unhex(j[game].at("protectedPassword").get<std::string>());
    auto entropy = binding(game, endpoint, pin);
    DATA_BLOB in{DWORD(encrypted.size()), encrypted.data()},
        extra{DWORD(entropy.size()), (BYTE *)entropy.data()}, out{};
    if (!CryptUnprotectData(&in, nullptr, &extra, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        throw Error("credentials", "Local password unavailable. Enter it again.");
    std::string password((char *)out.pbData, out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    if (password.size() < 12 || password.size() > 256 || password.find('\0') != std::string::npos) {
        erase(password);
        throw Error("credentials", "Invalid local password.");
    }
    return password;
}
void PasswordStore::put(const std::string &game, const std::string &endpoint, const std::string &pin,
                        std::string_view password) {
    auto j = load();
    if ((!j.contains(game) && j.size() >= 64) || password.size() < 12 || password.size() > 256)
        throw Error("credentials", "Local credential store is full or password is invalid.");
    auto entropy = binding(game, endpoint, pin);
    DATA_BLOB in{DWORD(password.size()), (BYTE *)password.data()},
        extra{DWORD(entropy.size()), (BYTE *)entropy.data()}, out{};
    if (!CryptProtectData(&in, L"Briefcase client administration", &extra, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out))
        throw Error("credentials", "Local protection unavailable.");
    auto encoded = hex(Bytes(out.pbData, out.pbData + out.cbData));
    LocalFree(out.pbData);
    j[game] = {{"endpoint", endpoint}, {"fingerprint", pin}, {"protectedPassword", encoded}};
    assert_plain_path(file.parent_path());
    fs::create_directories(file.parent_path());
    write_file(file, j.dump(2) + "\n", true, true);
}
void PasswordStore::forget(const std::string &game) {
    auto j = load();
    if (j.erase(game))
        write_file(file, j.dump(2) + "\n", true, true);
}
} // namespace bc::admin
