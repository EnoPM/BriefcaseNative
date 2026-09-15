#pragma once
#include <Briefcase/StartupApi.h>
#include <filesystem>
#include <span>
#include <string>
#include <vector>
namespace bc {
struct FileChange {
    std::filesystem::path path;
    std::string before, after;
};
struct ImmediateChange {
    uint32_t rva;
    int32_t before, after;
};
bool is_mov_i32(std::span<const uint8_t> window, uint32_t operand_offset);
std::vector<ImmediateChange> validate_patches(uint8_t *image, uint32_t image_size,
                                              std::span<const BcImmediatePatch> requests);
void commit_startup(uint8_t *image, const std::vector<ImmediateChange> &patches,
                    const std::vector<FileChange> &files);
std::string read_bounded(const std::filesystem::path &path, size_t limit);
void assert_plain_path(const std::filesystem::path &path);
} // namespace bc
