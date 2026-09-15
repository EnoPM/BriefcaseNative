#pragma once
#include <string_view>
// Synthetic protocol fixtures: bounded integer, boolean and floating-point settings.
namespace sample_alpha { inline constexpr std::string_view schema = R"({
 "type":"object","properties":{
  "itemLimit":{"type":"integer","minimum":1,"maximum":12,"default":12,"description":"Maximum items"},
  "batchLimit":{"type":"integer","minimum":1,"maximum":12,"default":12,"description":"Maximum items per batch"}
 }})"; }
namespace sample_beta { inline constexpr std::string_view schema = R"({
 "type":"object","properties":{
  "delayMilliseconds":{"type":"integer","minimum":1,"maximum":3600,"default":90,"description":"Refresh delay"},
  "diagnostics":{"type":"boolean","default":true,"description":"Enable diagnostics"}
 }})"; }
namespace sample_gamma { inline constexpr std::string_view schema = R"({
 "type":"object","properties":{
  "multiplier":{"type":"number","minimum":0.0,"maximum":10.0,"default":1.0,"description":"Display scale"},
  "diagnostics":{"type":"boolean","default":true,"description":"Enable diagnostics"},
  "maximumSamples":{"type":"integer","minimum":1,"maximum":1000,"default":120,"description":"Maximum measurements"}
 }})"; }
