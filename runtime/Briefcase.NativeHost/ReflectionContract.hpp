#pragma once
#include "Configuration.hpp"
namespace bc {
// Exact parameter layout, no wildcard signatures. A mod cannot silently invoke a changed function.
void validate_signature(const nlohmann::json &actual, const nlohmann::json &expected);
} // namespace bc
