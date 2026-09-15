#include "Startup.hpp"
#include <Windows.h>
#include <Zydis/Zydis.h>
#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>
namespace bc {
namespace fs = std::filesystem;
void assert_plain_path(const fs::path &path) {
    for (auto p = fs::absolute(path).lexically_normal(); !p.empty();) {
        auto a = GetFileAttributesW(p.c_str());
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Reparse point forbidden");
        auto parent = p.parent_path();
        if (parent == p)
            break;
        p = parent;
    }
}
std::string read_bounded(const fs::path &path, size_t limit) {
    assert_plain_path(path);
    if (fs::file_size(path) > limit)
        throw std::runtime_error("File size limit exceeded");
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("Cannot read file");
    std::string out((std::istreambuf_iterator<char>(in)), {});
    if (out.size() > limit || in.bad())
        throw std::runtime_error("Cannot read bounded file");
    return out;
}
bool is_mov_i32(std::span<const uint8_t> window, uint32_t operand_offset) {
    ZydisDecoder decoder{};
    if (!ZYAN_SUCCESS(ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64)))
        return false;
    size_t pos = 0;
    while (pos < window.size()) {
        ZydisDecodedInstruction inst{};
        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT]{};
        if (!ZYAN_SUCCESS(
                ZydisDecoderDecodeFull(&decoder, window.data() + pos, window.size() - pos, &inst, operands)))
            return false;
        if (inst.mnemonic == ZYDIS_MNEMONIC_MOV && inst.operand_count_visible == 2 &&
            operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER && operands[0].size == 32 &&
            operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && inst.raw.imm[0].size == 32 &&
            !inst.raw.imm[0].is_relative && pos + inst.raw.imm[0].offset == operand_offset)
            return true;
        pos += inst.length;
    }
    return false;
}
std::vector<ImmediateChange> validate_patches(uint8_t *image, uint32_t image_size,
                                              std::span<const BcImmediatePatch> requests) {
    if (requests.empty() || requests.size() > 64)
        throw std::runtime_error("Patch batch size invalid");
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        static_cast<uint64_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > image_size)
        throw std::runtime_error("Invalid PE image");
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(image + dos->e_lfanew);
    const auto first = IMAGE_FIRST_SECTION(nt);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.NumberOfSections > 96 ||
        reinterpret_cast<const uint8_t *>(first + nt->FileHeader.NumberOfSections) > image + image_size)
        throw std::runtime_error("Invalid PE sections");
    std::vector<ImmediateChange> result;
    std::set<uint32_t> occupied;
    for (const auto &req : requests) {
        if (req.size != sizeof(req) || req.reserved || !req.expected || req.window_size > 128 ||
            req.window_size < 5 || req.operand_offset + uint64_t(4) > req.window_size ||
            req.window_rva + uint64_t(req.window_size) > image_size)
            throw std::runtime_error("Invalid immediate descriptor");
        bool inText = false;
        for (unsigned n = 0; n < nt->FileHeader.NumberOfSections; ++n) {
            const auto &s = first[n];
            if (std::memcmp(s.Name, ".text\0\0\0", 8) == 0 && (s.Characteristics & IMAGE_SCN_MEM_EXECUTE) &&
                req.window_rva >= s.VirtualAddress &&
                req.window_rva + uint64_t(req.window_size) <= s.VirtualAddress + uint64_t(s.Misc.VirtualSize))
                inText = true;
        }
        if (!inText || std::memcmp(image + req.window_rva, req.expected, req.window_size) != 0 ||
            !is_mov_i32({req.expected, req.window_size}, req.operand_offset))
            throw std::runtime_error("Expected .text window / MOV imm32 mismatch");
        const auto target = req.window_rva + req.operand_offset;
        for (uint32_t i = 0; i < 4; ++i)
            if (!occupied.insert(target + i).second)
                throw std::runtime_error("Overlapping immediate patches");
        int32_t before;
        std::memcpy(&before, image + target, 4);
        result.push_back({target, before, req.replacement});
    }
    return result;
}
static void replace_file(const FileChange &file, bool rollback) {
    assert_plain_path(file.path);
    const auto temporary =
        fs::path(file.path.wstring() + L".briefcase." + std::to_wstring(GetCurrentProcessId()) + L".tmp");
    assert_plain_path(temporary);
    const auto &contents = rollback ? file.before : file.after;
    // CREATE_NEW prevents following or overwriting a pre-existing temporary file.
    HANDLE h =
        CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot create INI transaction file");
    DWORD written{};
    bool ok = WriteFile(h, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr) &&
              written == contents.size() && FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok || !MoveFileExW(temporary.c_str(), file.path.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        throw std::runtime_error("Atomic INI replacement failed");
    }
}
static void write_i32(uint8_t *address, int32_t value) {
    DWORD previous{}, unused{};
    if (!VirtualProtect(address, 4, PAGE_EXECUTE_READWRITE, &previous))
        throw std::runtime_error("VirtualProtect failed");
    std::memcpy(address, &value, 4);
    const auto flushed = FlushInstructionCache(GetCurrentProcess(), address, 4);
    const auto restored = VirtualProtect(address, 4, previous, &unused);
    if (!flushed || !restored)
        throw std::runtime_error("Instruction cache/protection restoration failed");
}
void commit_startup(uint8_t *image, const std::vector<ImmediateChange> &patches,
                    const std::vector<FileChange> &files) {
    for (const auto &patch : patches) {
        int32_t current;
        std::memcpy(&current, image + patch.rva, 4);
        if (current != patch.before)
            throw std::runtime_error("Instruction changed before commit");
    }
    for (const auto &file : files)
        if (read_bounded(file.path, 1048576) != file.before)
            throw std::runtime_error("INI changed before commit");
    size_t appliedFiles = 0, appliedPatches = 0;
    try {
        for (const auto &file : files) {
            if (file.before != file.after) {
                const auto backup = fs::path(file.path.wstring() + L".briefcase." +
                                             std::to_wstring(GetCurrentProcessId()) + L".bak");
                assert_plain_path(backup);
                if (!CopyFileW(file.path.c_str(), backup.c_str(), TRUE))
                    throw std::runtime_error("INI backup failed");
                replace_file(file, false);
            }
            ++appliedFiles;
        }
        for (const auto &patch : patches) {
            ++appliedPatches; // Include the current word if restoring page protection fails.
            write_i32(image + patch.rva, patch.after);
        }
        for (const auto &patch : patches) {
            int32_t actual;
            std::memcpy(&actual, image + patch.rva, 4);
            if (actual != patch.after)
                throw std::runtime_error("Instruction readback failed");
        }
        for (const auto &file : files)
            if (read_bounded(file.path, 1048576) != file.after)
                throw std::runtime_error("INI readback failed");
    } catch (...) {
        bool failed = false;
        while (appliedPatches) {
            const auto &p = patches[--appliedPatches];
            try {
                write_i32(image + p.rva, p.before);
            } catch (...) {
                failed = true;
            }
        }
        while (appliedFiles) {
            const auto &f = files[--appliedFiles];
            try {
                if (f.before != f.after)
                    replace_file(f, true);
            } catch (...) {
                failed = true;
            }
        }
        if (failed)
            throw std::runtime_error("Startup transaction failed; rollback incomplete; process must stop");
        throw;
    }
}
} // namespace bc
