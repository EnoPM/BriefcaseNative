#pragma once
namespace sample_settings {
inline constexpr const char* schema_text=R"({
 "type":"object","properties":{
  "enabled":{"type":"boolean","default":false,"description":"Show sample text","displayName":"Show sample text","displayNameKey":"mods.sample.settings.settings.enabled","category":"display","categoryLabel":"Display"},
  "opacity":{"type":"number","minimum":0,"maximum":1,"default":0.8,"description":"Opacity","displayName":"Opacity","category":"display","categoryLabel":"Display"},
  "hotkey":{"type":"integer","minimum":0,"maximum":255,"default":0,"description":"Shortcut","displayName":"Shortcut","control":"keybind","allowMouse":true,"category":"display","categoryLabel":"Display"}
 }})";
}
