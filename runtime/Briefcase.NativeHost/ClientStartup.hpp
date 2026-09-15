#pragma once
#include <string>
#include <string_view>
namespace bc {
// For the pinned game build, these signals are emitted after the shader-precompile screen.
class ClientStartupSignal {
    std::string tail_;
    bool ready_{};

  public:
    bool ready() const { return ready_; }
    bool feed(std::string_view chunk) {
        std::string text = tail_;
        text.append(chunk);
        if (text.find("USBShaderPrecompilerSubsytem::PrecompileCompleted") != text.npos ||
            text.find("Showing menu named MENU_LoginScreen") != text.npos ||
            text.find("Showing menu named MENU_MainMenu") != text.npos)
            ready_ = true;
        tail_ = text.substr(text.size() > 256 ? text.size() - 256 : 0);
        return ready_;
    }
};
} // namespace bc
