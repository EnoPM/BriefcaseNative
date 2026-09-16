#pragma once
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <nlohmann/json.hpp>

namespace bc::launcher {
namespace fs = std::filesystem;
using Json = nlohmann::json;
inline constexpr uint64_t max_archive = 512ull * 1024 * 1024;
inline constexpr const char* game_name = "DeceiveIncServer-Linux-Shipping";
void require(bool condition, const std::string& message);
fs::path plain(const fs::path& path);
fs::path package_path(const fs::path& root, const std::string& name);
bool managed(const std::string& name, bool legacy = false);
std::string read(const fs::path& path, uint64_t limit = 2 * 1024 * 1024);
Json document(const fs::path& path);
std::string digest(const fs::path& path);
void atomic(const fs::path& path, const std::string& data, unsigned mode = 0600);
void write_json(const fs::path& path, const Json& value);
std::string identifier();
void allowed_url(const std::string& url);
void download(const std::string& url, const fs::path& path, int timeout, uint64_t maximum);
std::optional<Json> select_asset(const Json& release, const std::string& repository, const std::string& current);
void extract(const fs::path& archive, const fs::path& stage);
Json package_manifest(const fs::path& stage, const std::string& version, const std::string& game_hash);

// Dependencies are injectable for deterministic offline contracts. The executable
// always uses the real HTTPS downloader; no environment switch bypasses validation.
using Download = std::function<void(const std::string&, const fs::path&, int, uint64_t)>;
class Updater {
public:
    explicit Updater(fs::path root, Download fetch = download);
    void recover();
    void install(const fs::path& stage, const Json& manifest,
                 const std::function<void(size_t)>& after_write = {});
    std::string update(const std::function<void(const std::string&)>& log);
private:
    fs::path root_;
    Download fetch_;
};
}
