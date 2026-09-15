#include "../runtime/Briefcase.Admin/Service.hpp"
#include "../runtime/Briefcase.Client.Servers/ServerDirectory.hpp"
#include <Windows.h>
#include <iostream>
#include <map>
#include <wincrypt.h>
using namespace bc::admin;
static std::string utf8(std::wstring_view s) {
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0, nullptr,
                                nullptr);
    if (n <= 0)
        throw std::runtime_error("Texte invalide.");
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), int(s.size()), out.data(), n, nullptr,
                        nullptr);
    return out;
}
static std::string password(const wchar_t *prompt) {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode{};
    if (!GetConsoleMode(input, &mode))
        throw std::runtime_error("La saisie du mot de passe exige un terminal interactif.");
    std::wcout << prompt << std::flush;
    if (!SetConsoleMode(input, (mode | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT) & ~ENABLE_ECHO_INPUT))
        throw std::runtime_error("Saisie masquee indisponible.");
    struct Restore {
        HANDLE h;
        DWORD mode;
        ~Restore() { SetConsoleMode(h, mode); }
    } restore{input, mode};
    wchar_t buffer[512]{};
    DWORD read{};
    struct Wipe {
        wchar_t *s;
        ~Wipe() { SecureZeroMemory(s, 512 * sizeof(wchar_t)); }
    } wipe{buffer};
    if (!ReadConsoleW(input, buffer, 511, &read, nullptr))
        throw std::runtime_error("Saisie interrompue.");
    std::wcout << L"\n";
    while (read && (buffer[read - 1] == L'\r' || buffer[read - 1] == L'\n'))
        --read;
    auto out = utf8(std::wstring_view(buffer, read));
    if (out.size() < 12 || out.size() > 256)
        throw std::runtime_error("Le mot de passe doit contenir de 12 a 256 octets UTF-8.");
    return out;
}
int wmain(int argc, wchar_t **argv) {
    try {
        std::map<std::wstring, std::wstring> args;
        bool generate_only = false;
        for (int i = 1; i < argc; ++i) {
            std::wstring option = argv[i];
            if (option == L"--generate-password") {
                if (generate_only)
                    throw std::runtime_error("Option repetee.");
                generate_only = true;
                continue;
            }
            if (option != L"--root" && option != L"--listen" && option != L"--port" &&
                option != L"--endpoint" && option != L"--generate-password-file")
                throw std::runtime_error("Option inconnue.");
            if (++i >= argc || !args.emplace(option, argv[i]).second)
                throw std::runtime_error("Option manquante ou repetee.");
        }
        if (generate_only && args.contains(L"--generate-password-file"))
            throw std::runtime_error("Choisir un seul mode de generation.");
        for (auto key : {L"--root", L"--listen", L"--port", L"--endpoint"})
            if (!args.contains(key))
                throw std::runtime_error("Option manquante.");
        auto root = fs::absolute(args.at(L"--root")).lexically_normal();
        if (root.filename() != L"Briefcase" || root.parent_path().filename() != L"Win64" ||
            !fs::exists(root.parent_path() / L"DeceiveIncServer-Win64-Shipping.exe"))
            throw std::runtime_error("Le dossier doit etre le Briefcase du Win64 d'un serveur dedie.");
        size_t consumed{};
        auto port = std::stoul(args.at(L"--port"), &consumed);
        if (consumed != args.at(L"--port").size() || port == 0 || port > 65535)
            throw std::runtime_error("Port invalide.");
        auto endpoint = bc::servers::normalize_endpoint(utf8(args.at(L"--endpoint")));
        // Refuse existing identities before generating a recoverable local password.
        if (fs::exists(root / L"Admin/server.json") || fs::exists(root / L"Admin/pairing.json"))
            throw std::runtime_error("Administration deja configuree.");
        const bool generated = generate_only || args.contains(L"--generate-password-file");
        std::string first;
        struct Wipe {
            std::string &s;
            ~Wipe() { erase(s); }
        } wipe{first};
        if (generated) {
            auto bytes = random_bytes(32);
            first = hex(bytes);
            erase(bytes);
        }
        if (generated && !generate_only) {
            const auto output = fs::absolute(args.at(L"--generate-password-file")).lexically_normal();
            if (!fs::is_directory(output.parent_path()) || fs::exists(output))
                throw std::runtime_error("Le fichier local doit etre nouveau, dans un dossier existant.");
            DATA_BLOB input{DWORD(first.size()), reinterpret_cast<BYTE *>(first.data())}, encrypted{};
            if (!CryptProtectData(&input, L"Briefcase local administrator password", nullptr, nullptr,
                                  nullptr, CRYPTPROTECT_UI_FORBIDDEN, &encrypted))
                throw std::runtime_error("Protection du mot de passe par Windows impossible.");
            struct FreeBlob {
                DATA_BLOB &blob;
                ~FreeBlob() {
                    SecureZeroMemory(blob.pbData, blob.cbData);
                    LocalFree(blob.pbData);
                }
            } freeBlob{encrypted};
            // Save first: setup failure leaves an unused protected password, never an inaccessible server.
            write_file(
                output,
                Json{{"version", 1}, {"protectedPassword", hex({encrypted.pbData, encrypted.cbData})}}.dump(
                    2),
                false, true);
        } else if (!generated) {
            first = password(L"Mot de passe administrateur (12 caracteres minimum) : ");
            std::string confirmation = password(L"Confirmer : ");
            bool match = first == confirmation;
            erase(confirmation);
            if (!match)
                throw std::runtime_error("Les mots de passe ne correspondent pas.");
        }
        auto settings = provision(root, utf8(args.at(L"--listen")), uint16_t(port), endpoint, first);
        std::wcout << L"Administration configuree pour le prochain demarrage du serveur.\n";
        std::cout << "Adresse : " << endpoint << "\nEmpreinte SHA-256 : " << settings.identity.fingerprint
                  << "\n";
        std::wcout << L"Fichier public a transmettre au client : " << (root / L"Admin/pairing.json") << L"\n";
        if (generated)
            std::wcout << L"Mot de passe genere : 256 bits, en clair dans Briefcase/Admin/server.json.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Erreur : " << e.what() << "\n";
        return 1;
    }
}
