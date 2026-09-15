#include "Startup.hpp"
#include "ExecutableImage.hpp"
#include "Platform.hpp"
#ifdef _WIN32
#include <Windows.h>
#else
#include "../Briefcase.Admin/Service.hpp"
#include <sys/mman.h>
#include <unistd.h>
#endif
#include <Zydis/Zydis.h>
#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>
namespace bc {
namespace fs = std::filesystem;
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
    std::vector<ImmediateChange> result;
    std::set<uint32_t> occupied;
    for (const auto &req : requests) {
        if (req.size != sizeof(req) || req.reserved || !req.expected || req.window_size > 128 ||
            req.window_size < 5 || req.operand_offset + uint64_t(4) > req.window_size ||
            req.window_rva + uint64_t(req.window_size) > image_size)
            throw std::runtime_error("Invalid immediate descriptor");
        const bool inText = executable_range(image, image_size, req.window_rva, req.window_size, true);
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
static bool scalar_window(std::span<const uint8_t> bytes, bool replacement) {
    ZydisDecoder decoder{};
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    std::set<size_t> boundaries{bytes.size()};
    std::vector<size_t> destinations;
    for(size_t at=0;at<bytes.size();) {
        boundaries.insert(at);
        ZydisDecodedInstruction instruction{};
        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT]{};
        if(!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder,bytes.data()+at,bytes.size()-at,&instruction,operands)))
            return false;
        if(replacement) {
            if(instruction.mnemonic != ZYDIS_MNEMONIC_MOV && instruction.mnemonic != ZYDIS_MNEMONIC_XOR &&
               instruction.mnemonic != ZYDIS_MNEMONIC_CMP && instruction.mnemonic != ZYDIS_MNEMONIC_NOP &&
               instruction.meta.category != ZYDIS_CATEGORY_CMOV &&
               instruction.meta.category != ZYDIS_CATEGORY_COND_BR) return false;
            if(instruction.meta.category==ZYDIS_CATEGORY_COND_BR) {
                if(instruction.operand_count_visible!=1 || operands[0].type!=ZYDIS_OPERAND_TYPE_IMMEDIATE ||
                   !operands[0].imm.is_relative || operands[0].imm.value.s<0) return false;
                const auto destination=at+instruction.length+operands[0].imm.value.s;
                if(destination>bytes.size()) return false;
                destinations.push_back(size_t(destination));
            }
            for(unsigned n=0;n<instruction.operand_count_visible;++n) {
                const auto& op=operands[n];
                if(op.type!=ZYDIS_OPERAND_TYPE_REGISTER && op.type!=ZYDIS_OPERAND_TYPE_IMMEDIATE) return false;
                if(op.type==ZYDIS_OPERAND_TYPE_REGISTER &&
                   (op.reg.value==ZYDIS_REGISTER_RSP || op.reg.value==ZYDIS_REGISTER_ESP ||
                    op.reg.value==ZYDIS_REGISTER_SP || op.reg.value==ZYDIS_REGISTER_SPL)) return false;
            }
        }
        at+=instruction.length;
    }
    for(auto destination:destinations) if(!boundaries.contains(destination)) return false;
    return true;
}
std::vector<CodeChange> validate_code_patches(uint8_t* image,uint32_t size,std::span<const BcCodePatch> requests) {
    if(requests.empty() || requests.size()>64) throw std::runtime_error("Invalid code patch count");
    std::vector<CodeChange> result;
    std::set<uint32_t> occupied;
    for(const auto& p:requests) {
        if(p.size!=sizeof(p) || p.reserved || !p.expected || !p.replacement || !p.window_size ||
           p.window_size>128 || !executable_range(image,size,p.window_rva,p.window_size,true) ||
           std::memcmp(image+p.window_rva,p.expected,p.window_size) ||
           !scalar_window({p.expected,p.window_size},false) ||
           !scalar_window({p.replacement,p.window_size},true))
            throw std::runtime_error("Code patch bytes/instructions mismatch");
        for(uint32_t i=0;i<p.window_size;++i)
            if(!occupied.insert(p.window_rva+i).second) throw std::runtime_error("Overlapping code patches");
        result.push_back({p.window_rva,{p.expected,p.expected+p.window_size},{p.replacement,p.replacement+p.window_size}});
    }
    return result;
}
static void replace_file(const FileChange &file, bool rollback) {
    assert_plain_path(file.path);
#ifdef _WIN32
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
#else
    admin::write_file(file.path, rollback ? file.before : file.after, true);
#endif
}
static void write_code(uint8_t *address, const void* bytes, size_t length) {
#ifdef _WIN32
    DWORD previous{}, unused{};
    if (!VirtualProtect(address, length, PAGE_EXECUTE_READWRITE, &previous))
        throw std::runtime_error("VirtualProtect failed");
    std::memcpy(address, bytes, length);
    const auto flushed = FlushInstructionCache(GetCurrentProcess(), address, length);
    const auto restored = VirtualProtect(address, length, previous, &unused);
    if (!flushed || !restored)
        throw std::runtime_error("Instruction cache/protection restoration failed");
#else
    const auto page_size = static_cast<uintptr_t>(sysconf(_SC_PAGESIZE));
    const auto first = reinterpret_cast<uintptr_t>(address) & ~(page_size-1);
    const auto last = (reinterpret_cast<uintptr_t>(address)+length+page_size-1) & ~(page_size-1);
    // Startup transactions run before the game entry and before backend workers.
    if(mprotect(reinterpret_cast<void*>(first),last-first,PROT_READ|PROT_WRITE|PROT_EXEC))
        throw std::runtime_error("Cannot make startup code writable");
    std::memcpy(address,bytes,length);
    __builtin___clear_cache(reinterpret_cast<char*>(address),reinterpret_cast<char*>(address+length));
    if(mprotect(reinterpret_cast<void*>(first),last-first,PROT_READ|PROT_EXEC))
        throw std::runtime_error("Cannot restore startup code protection");
#endif
}
void commit_startup(uint8_t *image, const std::vector<ImmediateChange> &patches,
                    const std::vector<FileChange> &files, const std::vector<CodeChange>& code) {
    std::set<uint32_t> occupied;
    for(const auto& patch:patches)
        for(uint32_t i=0;i<4;++i) occupied.insert(patch.rva+i);
    for(const auto& patch:code) {
        if(std::memcmp(image+patch.rva,patch.before.data(),patch.before.size()))
            throw std::runtime_error("Code changed before commit");
        for(uint32_t i=0;i<patch.before.size();++i)
            if(!occupied.insert(patch.rva+i).second) throw std::runtime_error("Overlapping transaction patches");
    }
    for (const auto &patch : patches) {
        int32_t current;
        std::memcpy(&current, image + patch.rva, 4);
        if (current != patch.before)
            throw std::runtime_error("Instruction changed before commit");
    }
    for (const auto &file : files)
        if (read_bounded(file.path, 1048576) != file.before)
            throw std::runtime_error("INI changed before commit");
    size_t appliedFiles = 0, appliedPatches = 0, appliedCode = 0;
    try {
        for (const auto &file : files) {
            if (file.before != file.after) {
                const auto backup = fs::path(file.path.string() + ".briefcase." +
                                             std::to_string(platform::process_id()) + ".bak");
                assert_plain_path(backup);
                if (!fs::copy_file(file.path, backup, fs::copy_options::none))
                    throw std::runtime_error("INI backup failed");
                replace_file(file, false);
            }
            ++appliedFiles;
        }
        for (const auto &patch : patches) {
            ++appliedPatches; // Include the current word if restoring page protection fails.
            write_code(image + patch.rva, &patch.after, 4);
        }
        for (const auto &patch : patches) {
            int32_t actual;
            std::memcpy(&actual, image + patch.rva, 4);
            if (actual != patch.after)
                throw std::runtime_error("Instruction readback failed");
        }
        for(const auto& patch:code) {
            ++appliedCode;
            write_code(image+patch.rva,patch.after.data(),patch.after.size());
            if(std::memcmp(image+patch.rva,patch.after.data(),patch.after.size()))
                throw std::runtime_error("Code readback mismatch");
        }
        for (const auto &file : files)
            if (read_bounded(file.path, 1048576) != file.after)
                throw std::runtime_error("INI readback failed");
    } catch (...) {
        bool failed = false;
        while(appliedCode) {
            const auto& p=code[--appliedCode];
            try { write_code(image+p.rva,p.before.data(),p.before.size()); }
            catch(...) { failed=true; }
        }
        while (appliedPatches) {
            const auto &p = patches[--appliedPatches];
            try {
                write_code(image + p.rva, &p.before, 4);
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
