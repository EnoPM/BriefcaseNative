#include "ReflectionContract.hpp"
#include <set>
namespace bc {
void validate_signature(const nlohmann::json &actual, const nlohmann::json &expected) {
    if (!expected.is_object() || !expected.at("parameterSize").is_number_unsigned() ||
        !expected.at("parameters").is_array() || expected.at("parameters").size() > 32 ||
        expected.at("parameterSize") != actual.at("parameterSize") ||
        expected.at("parameters").size() != actual.at("parameters").size())
        throw std::runtime_error("Function parameter size/count mismatch");
    if (expected.contains("flags") && expected.at("flags") != actual.at("flags"))
        throw std::runtime_error("Function flags mismatch");
    std::set<std::string> names;
    for (const auto &p : expected.at("parameters")) {
        auto name = p.at("name").get<std::string>();
        if (name.empty() || !names.insert(name).second)
            throw std::runtime_error("Duplicate parameter name");
        const nlohmann::json *match = nullptr;
        for (const auto &a : actual.at("parameters"))
            if (a.at("name") == name)
                match = &a;
        if (!match || p.at("type") != match->at("type") || p.at("offset") != match->at("offset") ||
            p.value("return", false) != match->value("return", false) ||
            p.value("output", false) != match->value("output", false))
            throw std::runtime_error("Function parameter layout/type mismatch: " + name);
    }
}
} // namespace bc
