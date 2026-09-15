# Menu shortcut

In Settings, click the current shortcut under Open / close the menu, then press
a keyboard key. Escape cancels. Restore F1 resets the default. The menu binding
uses a single key without modifiers; mouse keys and modifier-only keys are not
accepted for this action. System combinations such as Alt+Tab, Alt+F4, Shift+Tab
and Windows shortcuts retain their normal routing.

The host loads menuKey from Briefcase/ui-settings.json before rendering starts.
Missing or invalid values fall back to VK_F1 (112). Saving runs on the existing
preferences worker, together with language selection. The live binding changes
only after the atomic file write succeeds. Other preference fields are preserved.
No graphics resources or translation catalogues are loaded for the initial read.

Keybind.hpp is the shared ImGui control; KeybindState.hpp owns its small context
state, and KeyMap.hpp maps public VK values to ImGui input events. Input/Keys.hpp
validates bindings. The private host bridge v7 adds menu_key / set_menu_key; the
public mod ABI is unchanged.

Validation:
- ClientInputFixture runs the actual WndProc and polling filters with fake OS state:
  alternate key, repeats, binding capture, modifier changes, owned key releases,
  gameplay releases, focus loss/return and system shortcuts.
- ClientMenuFixture drives the rendered selector: assignment, Escape, held keys,
  current menu key, restore F1, focus loss, page change and translated labels.
- LocalizationContracts checks default/preloaded binding, saving with language,
  restart, invalid data fallback and failed writes leaving the prior key active.
