#include "Startup.hpp"
#include "NativeContract.hpp"
#include "ExecutableImage.hpp"
#include <sys/mman.h>
#include <elf.h>
#include <cstring>
#include <iostream>
#include <array>
static int checks;
void check(bool b) { ++checks; if(!b) throw std::runtime_error("Native Linux check "+std::to_string(checks)); }
template<class F> void rejects(F f) { bool denied=false;try { f(); } catch(...) {denied=true;}check(denied); }
int main() {
    try {
        auto* image=static_cast<uint8_t*>(mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
        check(image!=MAP_FAILED);
        struct Free { void* p; ~Free(){munmap(p,4096);} } free{image};
        auto* header=reinterpret_cast<Elf64_Ehdr*>(image);
        std::memcpy(header->e_ident,ELFMAG,SELFMAG);header->e_ident[EI_CLASS]=ELFCLASS64;
        header->e_ident[EI_DATA]=ELFDATA2LSB;header->e_machine=EM_X86_64;header->e_type=ET_EXEC;
        header->e_phoff=64;header->e_phentsize=sizeof(Elf64_Phdr);header->e_phnum=1;
        auto* segment=reinterpret_cast<Elf64_Phdr*>(image+64);
        segment->p_type=PT_LOAD;segment->p_vaddr=0x200000;segment->p_memsz=4096;segment->p_flags=PF_R|PF_X;
        const uint8_t original[]={0xb8,8,0,0,0,0xc3};
        std::memcpy(image+512,original,sizeof(original));
        const BcImmediatePatch request{sizeof(request),512,original,5,1,12,0};
        auto patches=bc::validate_patches(image,4096,{&request,1});
        check(patches.size()==1 && patches[0].before==8);
        auto invalid=request;invalid.operand_offset=2;
        rejects([&]{bc::validate_patches(image,4096,{&invalid,1});});
        segment->p_flags=PF_R;
        rejects([&]{bc::validate_patches(image,4096,{&request,1});});
        segment->p_flags=PF_R|PF_X;
        check(mprotect(image,4096,PROT_READ|PROT_EXEC)==0);
        bc::commit_startup(image,patches,{});
        auto function=reinterpret_cast<int(*)()>(image+512);
        check(function()==12);
        rejects([&]{bc::commit_startup(image,patches,{});});
        const uint8_t before[]={0xb8,12,0,0,0}, after[]={0xb8,21,0,0,0};
        BcCodePatch code{sizeof(code),512,5,0,before,after};
        auto changes=bc::validate_code_patches(image,4096,{&code,1});
        bc::commit_startup(image,{}, {}, changes);
        check(function()==21);
        const uint8_t illegal[]={0xe8,0,0,0,0};
        code.expected=after;code.replacement=illegal;
        rejects([&]{bc::validate_code_patches(image,4096,{&code,1});});
        const uint8_t outside[]={0x75,0x7f,0x90,0x90,0x90};
        code.replacement=outside;
        rejects([&]{bc::validate_code_patches(image,4096,{&code,1});});
        const uint8_t backwards[]={0x75,0xfe,0x90,0x90,0x90};
        code.replacement=backwards;
        rejects([&]{bc::validate_code_patches(image,4096,{&code,1});});
        const uint8_t middle[]={0x75,1,0xb0,1,0x90};
        code.replacement=middle;
        rejects([&]{bc::validate_code_patches(image,4096,{&code,1});});
        const uint8_t stack[]={0xbc,0,0,0,0};
        code.replacement=stack;
        rejects([&]{bc::validate_code_patches(image,4096,{&code,1});});
        const auto current=bc::executable_image();
        check(current.base && current.size);
        std::cout<<"PASS "<<checks<<" Linux executable/transaction checks\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}
}
