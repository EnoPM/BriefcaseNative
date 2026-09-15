#pragma once
#include "../Briefcase.Client.Rendering/ClientBridge.h"
namespace bc::menu {
void style(float dpi);
void draw(const BcClientHostApi &, const BcClientMetrics &, bool &open);
} // namespace bc::menu
