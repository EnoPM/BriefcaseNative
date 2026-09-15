#include <polyhook2/Detour/x64Detour.hpp>
#include <cstdio>
#include <stdexcept>
struct Object { float value; unsigned calls; };
static uint64_t original_float{},original_void{};
static unsigned callbacks{};
__attribute__((noinline)) static void change(Object* self,float value) {
    asm volatile(".rept 24; nop; .endr" ::: "memory");
    self->value=value; ++self->calls;
}
__attribute__((noinline)) static void reset(Object* self) {
    asm volatile(".rept 24; nop; .endr" ::: "memory");
    self->value=42; ++self->calls;
}
static void float_hook(Object* self,float value) {
    ++callbacks;
    reinterpret_cast<void(*)(Object*,float)>(original_float)(self,value*2);
}
static void void_hook(Object* self) {
    ++callbacks;
    reinterpret_cast<void(*)(Object*)>(original_void)(self);
}
int main() {
    try {
        auto require=[](bool value){if(!value)throw std::runtime_error("Linux SysV detour contract failed");};
        PLH::x64Detour floating(reinterpret_cast<uint64_t>(&change),reinterpret_cast<uint64_t>(&float_hook),&original_float);
        PLH::x64Detour noargs(reinterpret_cast<uint64_t>(&reset),reinterpret_cast<uint64_t>(&void_hook),&original_void);
        for(auto* hook:{&floating,&noargs}) {
            hook->setDetourScheme(PLH::x64Detour::detour_scheme_t::INPLACE);
            hook->setIsFollowCallOnFnAddress(false);require(hook->hook());
        }
        Object object{};
        void(*volatile call_float)(Object*,float)=change;
        void(*volatile call_void)(Object*)=reset;
        call_float(&object,7);require(object.value==14 && object.calls==1 && callbacks==1);
        call_void(&object);require(object.value==42 && object.calls==2 && callbacks==2);
        require(floating.unHook() && noargs.unHook());
        call_float(&object,3);call_void(&object);
        require(object.value==42 && object.calls==4 && callbacks==2);
        std::puts("PASS SysV self/float arguments, trampoline calls, native void hook and removal");
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
