#include "NativeContract.hpp"
#include "ExecutableImage.hpp"
#include <Zydis/Zydis.h>
#include <cstring>
#include <stdexcept>
namespace bc {
void validate_native_site(const uint8_t *image, uint32_t image_size, const BcNativeSite &s) {
    if (!image || image_size < 64 || s.size != sizeof(s) || !s.exec_bytes ||
        !s.target_bytes || s.exec_size < 5 || s.exec_size > 512 || s.target_size < 16 ||
        s.target_size > 128 || uint64_t(s.branch_offset) + 5 > s.exec_size ||
        uint64_t(s.exec_rva) + s.exec_size > image_size ||
        uint64_t(s.target_rva) + s.target_size > image_size)
        throw std::runtime_error("Invalid native descriptor");
    const auto executable = [&](uint32_t rva, uint32_t size) {
        return executable_range(image, image_size, rva, size);
    };
    if (!executable(s.exec_rva, s.exec_size) || !executable(s.target_rva, s.target_size) ||
        std::memcmp(image + s.exec_rva, s.exec_bytes, s.exec_size) ||
        std::memcmp(image + s.target_rva, s.target_bytes, s.target_size))
        throw std::runtime_error("Native executable windows mismatch");
    ZydisDecoder decoder{};
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    for (size_t pos = 0; pos <= s.branch_offset;) {
        ZydisDecodedInstruction inst{};
        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT]{};
        if (!ZYAN_SUCCESS(
                ZydisDecoderDecodeFull(&decoder, s.exec_bytes + pos, s.exec_size - pos, &inst, operands)))
            throw std::runtime_error("Native exec decoding failed");
        if (pos == s.branch_offset) {
            if ((inst.mnemonic != ZYDIS_MNEMONIC_CALL && inst.mnemonic != ZYDIS_MNEMONIC_JMP) ||
                inst.operand_count_visible != 1 || operands[0].type != ZYDIS_OPERAND_TYPE_IMMEDIATE ||
                !operands[0].imm.is_relative || inst.raw.imm[0].size != 32 ||
                int64_t(s.exec_rva) + int64_t(pos) + inst.length + operands[0].imm.value.s != s.target_rva)
                throw std::runtime_error("Native branch target mismatch");
            return;
        }
        pos += inst.length;
    }
    throw std::runtime_error("Native branch is not an instruction boundary");
}
} // namespace bc
