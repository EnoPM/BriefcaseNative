#pragma once
#include <Windows.h>
#include <imgui.h>
namespace bc::menu::keybind {
inline ImGuiKey to_imgui(unsigned k) {
    if (k >= 'A' && k <= 'Z')
        return ImGuiKey(ImGuiKey_A + k - 'A');
    if (k >= '0' && k <= '9')
        return ImGuiKey(ImGuiKey_0 + k - '0');
    if (k >= VK_F1 && k <= VK_F24)
        return ImGuiKey(ImGuiKey_F1 + k - VK_F1);
    if (k >= VK_NUMPAD0 && k <= VK_NUMPAD9)
        return ImGuiKey(ImGuiKey_Keypad0 + k - VK_NUMPAD0);
    switch (k) {
    case VK_MULTIPLY:
        return ImGuiKey_KeypadMultiply;
    case VK_ADD:
        return ImGuiKey_KeypadAdd;
    case VK_SUBTRACT:
        return ImGuiKey_KeypadSubtract;
    case VK_DECIMAL:
        return ImGuiKey_KeypadDecimal;
    case VK_DIVIDE:
        return ImGuiKey_KeypadDivide;
    case VK_PAUSE:
        return ImGuiKey_Pause;
    case VK_CAPITAL:
        return ImGuiKey_CapsLock;
    case VK_NUMLOCK:
        return ImGuiKey_NumLock;
    case VK_SCROLL:
        return ImGuiKey_ScrollLock;
    case VK_SNAPSHOT:
        return ImGuiKey_PrintScreen;
    case VK_OEM_1:
        return ImGuiKey_Semicolon;
    case VK_OEM_2:
        return ImGuiKey_Slash;
    case VK_OEM_3:
        return ImGuiKey_GraveAccent;
    case VK_OEM_4:
        return ImGuiKey_LeftBracket;
    case VK_OEM_5:
        return ImGuiKey_Backslash;
    case VK_OEM_6:
        return ImGuiKey_RightBracket;
    case VK_OEM_7:
        return ImGuiKey_Apostrophe;
    case VK_OEM_102:
        return ImGuiKey_Oem102;
    case VK_LWIN:
        return ImGuiKey_LeftSuper;
    case VK_RWIN:
        return ImGuiKey_RightSuper;
    case VK_TAB:
        return ImGuiKey_Tab;
    case VK_LEFT:
        return ImGuiKey_LeftArrow;
    case VK_RIGHT:
        return ImGuiKey_RightArrow;
    case VK_UP:
        return ImGuiKey_UpArrow;
    case VK_DOWN:
        return ImGuiKey_DownArrow;
    case VK_PRIOR:
        return ImGuiKey_PageUp;
    case VK_NEXT:
        return ImGuiKey_PageDown;
    case VK_HOME:
        return ImGuiKey_Home;
    case VK_END:
        return ImGuiKey_End;
    case VK_INSERT:
        return ImGuiKey_Insert;
    case VK_DELETE:
        return ImGuiKey_Delete;
    case VK_BACK:
        return ImGuiKey_Backspace;
    case VK_SPACE:
        return ImGuiKey_Space;
    case VK_RETURN:
        return ImGuiKey_Enter;
    case VK_ESCAPE:
        return ImGuiKey_Escape;
    case VK_SHIFT:
    case VK_LSHIFT:
        return ImGuiKey_LeftShift;
    case VK_RSHIFT:
        return ImGuiKey_RightShift;
    case VK_CONTROL:
    case VK_LCONTROL:
        return ImGuiKey_LeftCtrl;
    case VK_RCONTROL:
        return ImGuiKey_RightCtrl;
    case VK_MENU:
    case VK_LMENU:
        return ImGuiKey_LeftAlt;
    case VK_RMENU:
        return ImGuiKey_RightAlt;
    case VK_OEM_PERIOD:
        return ImGuiKey_Period;
    case VK_OEM_COMMA:
        return ImGuiKey_Comma;
    case VK_OEM_MINUS:
        return ImGuiKey_Minus;
    case VK_OEM_PLUS:
        return ImGuiKey_Equal;
    default:
        return ImGuiKey_None;
    }
}
} // namespace bc::menu::keybind
