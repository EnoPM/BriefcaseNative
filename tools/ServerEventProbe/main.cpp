#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace {
struct Observation {
    std::string kind;
    std::string source;
    std::string game_time;
    std::string subject;
    std::string actor;
    std::string value;
    std::string evidence;
};

std::string json_string(const std::string &value) {
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char ch : value) {
        switch (ch) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (ch < 0x20) {
                result += "\\u00";
                result += hex[ch >> 4];
                result += hex[ch & 15];
            } else {
                result += static_cast<char>(ch);
            }
        }
    }
    return result + '"';
}

std::optional<Observation> classify(const std::string &line) {
    static const std::regex time(R"(^\[([^\]]+)\])");
    static const std::regex phase(R"(Spy Game Phase : \{ESpyGamePhase::([^}]+)\})");
    static const std::regex interaction(R"(CompleteInteraction \[([^\]]+)\]: <([^>]+)>$)");
    static const std::regex damage(
        R"(UHealthComponent::ProcessDamage.*\{([^}]+)\} Health Changed: ([^ ]+) \(([^)]+)\))");
    static const std::regex resource(
        R"(UGameplayResourcesComponent::(AddResource|RemoveResource).*:: ([0-9]+) Resource of type EGameplayResourcesType::(Mission_Objective|Keycard_[A-Za-z]+) (added|removed) to (BPSpy_[^ ]+))");
    static const std::regex match_result(R"re("matchResult"\s*:\s*"([^"]+)")re");

    std::smatch match;
    Observation result;
    result.evidence = line;
    if (std::regex_search(line, match, time)) result.game_time = match[1];
    if (line.find("Spy Game Phase :") != std::string::npos &&
        std::regex_search(line, match, phase)) {
        result.kind = "phase_changed";
        result.value = match[1];
    } else if (line.find("CompleteInteraction [") != std::string::npos &&
               std::regex_search(line, match, interaction)) {
        result.kind = "interaction_complete";
        result.subject = match[1];
        result.actor = match[2];
    } else if (line.find("UHealthComponent::ProcessDamage") != std::string::npos &&
               std::regex_search(line, match, damage)) {
        result.kind = "damage_observed";
        result.subject = match[1];
        result.value = match[3]; // Log-reported delta; not verified scored damage.
    } else if (line.find("EGameplayResourcesType::Mission_Objective") != std::string::npos ||
               line.find("EGameplayResourcesType::Keycard_") != std::string::npos) {
        if (!std::regex_search(line, match, resource)) return std::nullopt;
        result.kind = "resource_change_observed";
        result.subject = match[3];
        result.actor = match[5];
        result.value = std::string(match[4]) + ':' + std::string(match[2]);
    } else if (line.find("Unhandled Exception:") != std::string::npos) {
        result.kind = "crash_observed";
    } else if (line.find("\"matchResult\"") != std::string::npos &&
               std::regex_search(line, match, match_result)) {
        result.kind = "match_result_observed";
        result.value = match[1];
    } else {
        return std::nullopt;
    }
    return result;
}

void write(const Observation &observation, std::ostream &output,
           std::map<std::string, std::uint64_t> &counts) {
    output << "{\"kind\":" << json_string(observation.kind)
           << ",\"source\":" << json_string(observation.source)
           << ",\"game_time\":" << json_string(observation.game_time)
           << ",\"subject\":" << json_string(observation.subject)
           << ",\"actor\":" << json_string(observation.actor)
           << ",\"value\":" << json_string(observation.value)
           << ",\"evidence\":" << json_string(observation.evidence)
           << "}\n";
    output.flush();
    ++counts[observation.kind];
}

void consume(std::string &pending, const fs::path &source, std::ostream &output,
             std::map<std::string, std::uint64_t> &counts) {
    std::size_t end{};
    while ((end = pending.find('\n')) != std::string::npos) {
        std::string line = pending.substr(0, end);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pending.erase(0, end + 1);
        if (auto observation = classify(line)) {
            observation->source = source.string();
            write(*observation, output, counts);
        }
    }
    if (pending.size() > 1024 * 1024) pending.clear();
}

fs::path latest_log(const fs::path &directory) {
    fs::path latest;
    fs::file_time_type latest_time = fs::file_time_type::min();
    for (const auto &entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        const auto name = entry.path().filename().string();
        if (name.size() < 14 || name.compare(0, 10, "DeceiveInc") != 0 ||
            entry.path().extension() != ".log" || name.find("-backup-") != std::string::npos)
            continue;
        const auto modified = entry.last_write_time();
        if (modified > latest_time) {
            latest = entry.path();
            latest_time = modified;
        }
    }
    return latest;
}

void replay(const fs::path &input, std::ostream &output,
            std::map<std::string, std::uint64_t> &counts) {
    std::ifstream stream(input, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read log: " + input.string());
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (auto observation = classify(line)) {
            observation->source = input.string();
            write(*observation, output, counts);
        }
    }
}

void follow(const fs::path &directory, std::ostream &output,
            std::map<std::string, std::uint64_t> &counts) {
    if (!fs::is_directory(directory))
        throw std::runtime_error("Log directory does not exist: " + directory.string());
    fs::path active;
    std::uintmax_t offset{};
    std::string pending;
    bool initialized = false;
    for (;;) {
        const fs::path newest = latest_log(directory);
        if (!newest.empty()) {
            if (newest != active) {
                active = newest;
                // Skip old gameplay only at startup. Consume a rotated log from
                // its beginning, including lines written before this poll.
                offset = initialized ? 0 : fs::file_size(active);
                initialized = true;
                pending.clear();
                std::cerr << "Following " << active.string() << '\n';
            } else {
                const auto size = fs::file_size(active);
                if (size < offset) {
                    offset = 0;
                    pending.clear();
                }
                if (size > offset) {
                    std::ifstream stream(active, std::ios::binary);
                    if (stream) {
                        stream.seekg(static_cast<std::streamoff>(offset));
                        std::string chunk(std::min<std::uintmax_t>(size - offset, 1024 * 1024), '\0');
                        stream.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
                        chunk.resize(static_cast<std::size_t>(stream.gcount()));
                        offset += chunk.size();
                        pending += chunk;
                        consume(pending, active, output, counts);
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 3 && argc != 5)
            throw std::runtime_error("Usage: Briefcase.ServerEventProbe --replay <log> | --follow <log-directory> --output <jsonl> | --check-fixture <log>");
        std::ofstream file;
        std::ostream *output = &std::cout;
        if (argc == 5) {
            if (std::string(argv[3]) != "--output") throw std::runtime_error("Expected --output");
            file.open(argv[4], std::ios::binary |
                (std::string(argv[1]) == "--replay" ? std::ios::trunc : std::ios::app));
            if (!file) throw std::runtime_error("Cannot open output file");
            output = &file;
        }
        std::map<std::string, std::uint64_t> counts;
        const std::string mode = argv[1];
        if (mode == "--check-fixture") {
            std::ostringstream sink;
            replay(argv[2], sink, counts);
            if (counts["phase_changed"] != 1 || counts["interaction_complete"] != 1 ||
                counts["damage_observed"] != 1 || counts["crash_observed"] != 1 ||
                counts["match_result_observed"] != 1 ||
                counts["resource_change_observed"] != 1)
                throw std::runtime_error("Fixture classification failed");
            std::cout << "Fixture classification passed\n";
        } else if (mode == "--replay") {
            replay(argv[2], *output, counts);
            for (const auto &[kind, count] : counts) std::cerr << kind << '=' << count << '\n';
        } else if (mode == "--follow") {
            if (argc != 5) throw std::runtime_error("--follow requires --output");
            follow(argv[2], *output, counts);
        } else {
            throw std::runtime_error("Unknown mode: " + mode);
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
