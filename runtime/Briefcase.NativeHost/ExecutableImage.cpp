#include "ExecutableImage.hpp"
#include <cstring>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#include <Windows.h>
#else
#include <elf.h>
#include <link.h>
#endif
namespace bc {
bool executable_range(const uint8_t* image, uint32_t size, uint32_t rva, uint32_t length, bool text_only) {
    if (!image || !length || uint64_t(rva) + length > size) return false;
#ifdef _WIN32
    if (size < sizeof(IMAGE_DOS_HEADER)) return false;
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        uint64_t(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > size) return false;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
    auto sections = IMAGE_FIRST_SECTION(nt);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->FileHeader.NumberOfSections > 96 ||
        reinterpret_cast<const uint8_t*>(sections + nt->FileHeader.NumberOfSections) > image + size) return false;
    for (unsigned i=0; i<nt->FileHeader.NumberOfSections; ++i) {
        const auto& s=sections[i];
        if ((s.Characteristics & IMAGE_SCN_MEM_EXECUTE) &&
            (!text_only || std::memcmp(s.Name, ".text\0\0\0", 8)==0) &&
            rva >= s.VirtualAddress && uint64_t(rva)+length <= uint64_t(s.VirtualAddress)+s.Misc.VirtualSize)
            return true;
    }
#else
    (void)text_only;
    if (size < sizeof(Elf64_Ehdr)) return false;
    const auto* h=reinterpret_cast<const Elf64_Ehdr*>(image);
    if (std::memcmp(h->e_ident, ELFMAG, SELFMAG) || h->e_ident[EI_CLASS]!=ELFCLASS64 ||
        h->e_ident[EI_DATA]!=ELFDATA2LSB || h->e_machine!=EM_X86_64 ||
        (h->e_type!=ET_EXEC && h->e_type!=ET_DYN) ||
        h->e_phentsize!=sizeof(Elf64_Phdr) || h->e_phnum>128 ||
        h->e_phoff>size || uint64_t(h->e_phnum)*sizeof(Elf64_Phdr)>size-h->e_phoff) return false;
    const auto* segments=reinterpret_cast<const Elf64_Phdr*>(image+h->e_phoff);
    uint64_t origin=UINT64_MAX;
    for (unsigned i=0;i<h->e_phnum;++i)
        if (segments[i].p_type==PT_LOAD && segments[i].p_offset==0) origin=segments[i].p_vaddr;
    if (origin==UINT64_MAX) return false;
    for (unsigned i=0;i<h->e_phnum;++i) {
        const auto& s=segments[i];
        if (s.p_type!=PT_LOAD || !(s.p_flags&PF_X) || s.p_vaddr<origin) continue;
        const auto start=s.p_vaddr-origin;
        if (start<=rva && s.p_memsz<=UINT64_MAX-start && uint64_t(rva)+length<=start+s.p_memsz) return true;
    }
#endif
    return false;
}
ExecutableImage executable_image() {
#ifdef _WIN32
    auto* image=reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(image+reinterpret_cast<IMAGE_DOS_HEADER*>(image)->e_lfanew);
    return {image, nt->OptionalHeader.SizeOfImage};
#else
    ExecutableImage out;
    dl_iterate_phdr([](dl_phdr_info* info, size_t, void* user) {
        if (info->dlpi_name && *info->dlpi_name) return 0;
        uintptr_t first=UINTPTR_MAX, end=0;
        for(unsigned i=0;i<info->dlpi_phnum;++i) {
            const auto& p=info->dlpi_phdr[i];
            if(p.p_type!=PT_LOAD) continue;
            auto start=info->dlpi_addr+p.p_vaddr;
            if(p.p_offset==0) first=start;
            if(start+p.p_memsz>end) end=start+p.p_memsz;
        }
        if(first!=UINTPTR_MAX && end>first && end-first<=UINT32_MAX)
            *static_cast<ExecutableImage*>(user)={reinterpret_cast<uint8_t*>(first),uint32_t(end-first)};
        return 1;
    }, &out);
    if(!out.base) throw std::runtime_error("Unable to discover main ELF load segments");
    return out;
#endif
}
}
