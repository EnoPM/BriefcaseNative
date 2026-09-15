#include "Configuration.hpp"
#include "Manifest.hpp"
#include "Startup.hpp"
#include <Windows.h>
#include <cstring>
#include <fstream>
#include <iostream>
static int checks;
static void check(bool value) {
    ++checks;
    if (!value)
        throw std::runtime_error("Startup contract #" + std::to_string(checks));
}
template <class F> void rejects(F f) {
    bool failed = false;
    try {
        f();
    } catch (...) {
        failed = true;
    }
    check(failed);
}
int main() {
    try {
        using json = nlohmann::json;
        auto schema = bc::strict_json(R"({"type":"object","properties":{
      "b":{"type":"boolean","default":true,"description":"bool"},
      "i":{"type":"integer","minimum":1,"maximum":12,"default":12,"description":"int"},
      "f":{"type":"number","minimum":0,"maximum":10,"default":1.0,"description":"float"},
      "s":{"type":"string","default":"text","description":"string"},
      "e":{"type":"string","enum":["head","body"],"default":"body","description":"enum"},
      "l":{"type":"array","items":{"type":"string"},"default":[],"description":"list"}
    }})");
        auto defaults = bc::normalize_config(schema, json::object());
        check(defaults["i"] == 12 && defaults["b"] == true && defaults["l"].empty());
        auto good = defaults;
        good["i"] = 1;
        good["f"] = 0.1;
        good["e"] = "head";
        good["l"] = {"a", "b"};
        check(bc::normalize_config(schema, good) == good);
        good["i"] = 12;
        good["f"] = 10;
        check(bc::normalize_config(schema, good) == good);
        for (auto v : {json(0), json(13), json(1.5), json(true), json("12"), json(uint64_t(-1))}) {
            auto bad = defaults;
            bad["i"] = v;
            rejects([&] { bc::normalize_config(schema, bad); });
        }
        for (auto [key, value] : std::vector<std::pair<std::string, json>>{{"b", 1},
                                                                           {"f", -0.01},
                                                                           {"f", 10.1},
                                                                           {"f", "0.5"},
                                                                           {"s", false},
                                                                           {"e", "other"},
                                                                           {"l", {1, "a"}},
                                                                           {"extra", true}}) {
            auto bad = defaults;
            bad[key] = value;
            rejects([&] { bc::normalize_config(schema, bad); });
        }
        rejects([&] { bc::strict_json(R"({"solo":8,"solo":12})"); });
        auto invalid = schema;
        invalid["properties"]["i"]["default"] = 13;
        rejects([&] { bc::normalize_config(invalid, json::object()); });
        auto ini = std::string("; keep\r\n[server]\r\nGameMode=Solo\r\n  MaxPlayers = 8  "
                               "\r\nSecret=unchanged\r\n[other]\r\nMaxPlayers=4");
        check(bc::ini_read(ini, "server", "GameMode") == "Solo");
        auto updated = bc::ini_write(ini, "server", "MaxPlayers", "12");
        check(updated == "; keep\r\n[server]\r\nGameMode=Solo\r\n  MaxPlayers = 12  "
                         "\r\nSecret=unchanged\r\n[other]\r\nMaxPlayers=4");
        check(bc::ini_write(updated, "server", "MaxPlayers", "12") == updated);
        check(bc::ini_write("[server]\nMode=Solo\n[other]\nX=1\n", "server", "MaxPlayers", "12") ==
              "[server]\nMode=Solo\nMaxPlayers=12\n[other]\nX=1\n");
        check(bc::ini_write("[server]", "server", "MaxPlayers", "12") == "[server]\nMaxPlayers=12");
        check(bc::ini_write("[server]\n", "server", "MaxPlayers", "12") == "[server]\nMaxPlayers=12\n");
        rejects([&] { bc::ini_write("[server]\nX=1\nX=2", "server", "X", "3"); });
        rejects([&] { bc::ini_write("[server]\nX=1\n[server]", "server", "X", "3"); });
        rejects([&] { bc::ini_write("[server]", "server", "X", "1\nInjected=1"); });
        rejects([&] { bc::ini_write("[different]", "server", "X", "1"); });
        const uint8_t mov[] = {0x41, 0xb9, 0x08, 0, 0, 0};
        check(bc::is_mov_i32(mov, 2));
        check(!bc::is_mov_i32(mov, 1));
        const uint8_t call[] = {0xe8, 0, 0, 0, 0};
        check(!bc::is_mov_i32(call, 1));
        const uint8_t store[] = {0xc7, 0x01, 0x08, 0, 0, 0};
        check(!bc::is_mov_i32(store, 2));
        const auto image =
            static_cast<uint8_t *>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        check(image != nullptr);
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(image);
        dos->e_magic = IMAGE_DOS_SIGNATURE;
        dos->e_lfanew = 128;
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(image + 128);
        nt->Signature = IMAGE_NT_SIGNATURE;
        nt->FileHeader.NumberOfSections = 1;
        nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
        auto section = IMAGE_FIRST_SECTION(nt);
        std::memcpy(section->Name, ".text", 6);
        section->VirtualAddress = 1024;
        section->Misc.VirtualSize = 1024;
        section->Characteristics = IMAGE_SCN_MEM_EXECUTE;
        std::memcpy(image + 1024, mov, sizeof(mov));
        BcImmediatePatch request{sizeof(BcImmediatePatch), 1024, mov, sizeof(mov), 2, 12, 0};
        auto patches = bc::validate_patches(image, 4096, {&request, 1});
        check(patches.size() == 1 && patches[0].before == 8 && patches[0].rva == 1026);
        auto bad = request;
        bad.window_rva = 2046;
        rejects([&] { bc::validate_patches(image, 4096, {&bad, 1}); });
        bad = request;
        bad.operand_offset = 1;
        rejects([&] { bc::validate_patches(image, 4096, {&bad, 1}); });
        BcImmediatePatch overlap[] = {request, request};
        rejects([&] { bc::validate_patches(image, 4096, overlap); });
        image[1026] = 7;
        rejects([&] { bc::validate_patches(image, 4096, {&request, 1}); });
        image[1026] = 8;
        const auto dir =
            std::filesystem::current_path() / ("startup-fixture-" + std::to_string(GetCurrentProcessId()));
        std::filesystem::create_directory(dir);
        const auto path = dir / "settings.ini";
        {
            std::ofstream out(path, std::ios::binary);
            out << "[server]\nX=8\n";
        }
        auto before = bc::read_bounded(path, 1024);
        bc::FileChange file{path, before, "[server]\nX=12\n"};
        auto changed = file;
        changed.before = "wrong";
        rejects([&] { bc::commit_startup(image, patches, {changed}); });
        check(image[1026] == 8 && bc::read_bounded(path, 1024) == before);
        bc::commit_startup(image, patches, {file});
        check(image[1026] == 12 && bc::read_bounded(path, 1024) == file.after);
        MEMORY_BASIC_INFORMATION mbi{};
        VirtualQuery(image, &mbi, sizeof(mbi));
        check(mbi.Protect == PAGE_READWRITE);
        // Remove only this fixture's first backup so the next transaction can reach its second file.
        const auto backup = std::filesystem::path(path.wstring() + L".briefcase." +
                                                  std::to_wstring(GetCurrentProcessId()) + L".bak");
        check(std::filesystem::remove(backup));
        image[1026] = 8;
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << before;
        }
        // File replacement failure after the first file: existing CREATE_NEW temp is never overwritten.
        const auto second = dir / "second.ini";
        {
            std::ofstream out(second, std::ios::binary);
            out << before;
        }
        const auto blocked = std::filesystem::path(second.wstring() + L".briefcase." +
                                                   std::to_wstring(GetCurrentProcessId()) + L".tmp");
        {
            std::ofstream out(blocked, std::ios::binary);
            out << "do not overwrite";
        }
        bc::FileChange other{second, before, file.after};
        rejects([&] { bc::commit_startup(image, patches, {file, other}); });
        check(image[1026] == 8 && bc::read_bounded(path, 1024) == before &&
              bc::read_bounded(blocked, 1024) == "do not overwrite");
        VirtualFree(image, 0, MEM_RELEASE);
        // Fixture directory is a freshly generated child of this build directory.
        for (const auto &item : std::filesystem::directory_iterator(dir))
            std::filesystem::remove(item.path());
        std::filesystem::remove(dir);
        std::cout << "PASS " << checks << " startup/configuration contract checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
