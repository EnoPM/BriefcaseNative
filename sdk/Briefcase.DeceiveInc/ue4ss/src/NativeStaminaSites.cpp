#include "NativeStaminaSites.hpp"

#include <Zydis/Zydis.h>
#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>

namespace briefcase::deceive::detail {
namespace {
struct Instruction {
    ZydisDecodedInstruction code{};
    std::array<ZydisDecodedOperand, ZYDIS_MAX_OPERAND_COUNT> operands{};
    std::size_t offset{};
};

bool decode(const ZydisDecoder &decoder, std::span<const std::uint8_t> bytes,
            std::size_t &cursor, Instruction &out) {
    if (cursor >= bytes.size()) return false;
    out = {};
    out.offset = cursor;
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, bytes.data() + cursor,
                                               bytes.size() - cursor, &out.code,
                                               out.operands.data()))) return false;
    cursor += out.code.length;
    return true;
}

bool spy_argument(const Instruction &instruction) {
    if (instruction.code.mnemonic != ZYDIS_MNEMONIC_MOV ||
        instruction.code.operand_count_visible != 2 ||
        instruction.operands[0].type != ZYDIS_OPERAND_TYPE_REGISTER ||
        instruction.operands[0].reg.value != ZYDIS_REGISTER_RCX ||
        instruction.operands[1].type != ZYDIS_OPERAND_TYPE_REGISTER) return false;
    switch (instruction.operands[1].reg.value) {
    case ZYDIS_REGISTER_RBX:
    case ZYDIS_REGISTER_RBP:
    case ZYDIS_REGISTER_RSI:
    case ZYDIS_REGISTER_RDI:
    case ZYDIS_REGISTER_R12:
    case ZYDIS_REGISTER_R13:
    case ZYDIS_REGISTER_R14:
    case ZYDIS_REGISTER_R15: return true;
    default: return false;
    }
}

bool delta_argument(const Instruction &instruction) {
    return instruction.code.mnemonic == ZYDIS_MNEMONIC_MOVSS &&
           instruction.code.operand_count_visible == 2 &&
           instruction.operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
           instruction.operands[0].reg.value == ZYDIS_REGISTER_XMM1 &&
           instruction.operands[1].type == ZYDIS_OPERAND_TYPE_MEMORY &&
           instruction.operands[1].mem.base == ZYDIS_REGISTER_RSP;
}

std::optional<std::uintptr_t> direct_call(const Instruction &instruction,
                                           std::uintptr_t instruction_address) {
    if (instruction.code.mnemonic != ZYDIS_MNEMONIC_CALL ||
        instruction.code.operand_count_visible != 1 ||
        instruction.operands[0].type != ZYDIS_OPERAND_TYPE_IMMEDIATE ||
        !instruction.operands[0].imm.is_relative) return std::nullopt;
    const auto target = static_cast<std::int64_t>(instruction_address) +
                        instruction.code.length + instruction.operands[0].imm.value.s;
    if (target <= 0) return std::nullopt;
    return static_cast<std::uintptr_t>(target);
}
} // namespace

std::uintptr_t find_native_reduce_stamina(std::span<const std::uint8_t> executable_text,
                                          std::uintptr_t text_address,
                                          std::uintptr_t thunk_address) {
    if (!text_address || thunk_address < text_address ||
        thunk_address - text_address >= executable_text.size())
        throw std::runtime_error("Reflected ReduceStamina thunk is outside game code");

    const auto start = static_cast<std::size_t>(thunk_address - text_address);
    const auto code = executable_text.subspan(start, std::min<std::size_t>(256, executable_text.size() - start));
    ZydisDecoder decoder{};
    if (!ZYAN_SUCCESS(ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64,
                                       ZYDIS_STACK_WIDTH_64)))
        throw std::runtime_error("Cannot initialize ReduceStamina decoder");

    std::size_t cursor{};
    std::size_t last_delta = code.size();
    Instruction previous{};
    std::optional<std::uintptr_t> candidate;
    bool returned{};
    while (cursor < code.size()) {
        Instruction instruction{};
        if (!decode(decoder, code, cursor, instruction))
            throw std::runtime_error("Cannot decode reflected ReduceStamina thunk");
        if (delta_argument(instruction)) last_delta = instruction.offset;
        if (auto target = direct_call(instruction, thunk_address + instruction.offset);
            target && spy_argument(previous) && last_delta < instruction.offset &&
            instruction.offset - last_delta <= 64) {
            if (*target < text_address || *target - text_address >= executable_text.size() ||
                *target >= thunk_address && *target < thunk_address + code.size())
                throw std::runtime_error("ReduceStamina native call points outside game code");
            if (candidate)
                throw std::runtime_error("Ambiguous native ReduceStamina call");
            candidate = target;
        }
        if (instruction.code.mnemonic == ZYDIS_MNEMONIC_RET) {
            returned = true;
            break;
        }
        previous = instruction;
    }
    if (!returned || !candidate)
        throw std::runtime_error("Reflected ReduceStamina thunk layout changed");
    return *candidate;
}

std::uintptr_t find_native_reset_stamina(std::span<const std::uint8_t> executable_text,
                                         std::uintptr_t text_address,
                                         std::uintptr_t thunk_address) {
    if (!text_address || thunk_address < text_address ||
        thunk_address - text_address >= executable_text.size())
        throw std::runtime_error("Reflected ResetStaminaToMax thunk is outside game code");

    const auto start = static_cast<std::size_t>(thunk_address - text_address);
    const auto code = executable_text.subspan(start, std::min<std::size_t>(128, executable_text.size() - start));
    ZydisDecoder decoder{};
    if (!ZYAN_SUCCESS(ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64,
                                       ZYDIS_STACK_WIDTH_64)))
        throw std::runtime_error("Cannot initialize ResetStaminaToMax decoder");

    std::size_t cursor{};
    while (cursor < code.size()) {
        Instruction instruction{};
        if (!decode(decoder, code, cursor, instruction))
            throw std::runtime_error("Cannot decode reflected ResetStaminaToMax thunk");
        if (instruction.code.mnemonic == ZYDIS_MNEMONIC_JMP) {
            if (instruction.code.operand_count_visible != 1 ||
                instruction.operands[0].type != ZYDIS_OPERAND_TYPE_IMMEDIATE ||
                !instruction.operands[0].imm.is_relative)
                break;
            const auto destination = static_cast<std::int64_t>(thunk_address + instruction.offset) +
                                     instruction.code.length + instruction.operands[0].imm.value.s;
            if (destination <= 0)
                break;
            const auto target = static_cast<std::uintptr_t>(destination);
            if (target < text_address || target - text_address >= executable_text.size() ||
                (target >= thunk_address && target < thunk_address + code.size()))
                break;
            return target;
        }
        if (instruction.code.mnemonic == ZYDIS_MNEMONIC_RET ||
            instruction.code.meta.category == ZYDIS_CATEGORY_COND_BR ||
            instruction.code.meta.category == ZYDIS_CATEGORY_CALL ||
            instruction.code.meta.category == ZYDIS_CATEGORY_UNCOND_BR)
            break;
    }
    throw std::runtime_error("Reflected ResetStaminaToMax thunk layout changed");
}

} // namespace briefcase::deceive::detail
