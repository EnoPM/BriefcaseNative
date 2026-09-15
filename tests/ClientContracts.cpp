#include "../runtime/Briefcase.Client.Input/InputPolicy.hpp"
#include "../runtime/Briefcase.Client.Input/Keys.hpp"
#include "ClientStartup.hpp"
#include "GameProfile.hpp"
#include "Manifest.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
static int checks;
static void check(bool value) {
    ++checks;
    if (!value)
        throw std::runtime_error("Client contract " + std::to_string(checks));
}
int main() {
    try {
        using bc::Environment;
        for (auto mode : {Environment::server, Environment::client}) {
            check(bc::matches_environment("both", mode));
            check(bc::matches_environment(mode == Environment::client ? "client" : "server", mode));
            check(!bc::matches_environment(mode == Environment::client ? "server" : "client", mode));
            check(!bc::matches_environment("invalid", mode));
        }
        check(bc::game_profile(L"DeceiveInc-Win64-Shipping.exe", 0x6A96564B, 0x06283000) ==
              &bc::client_profile);
        check(bc::game_profile(L"DeceiveIncServer-Win64-Shipping.exe", 0x6A966107, 0x05B60000) ==
              &bc::server_profile);
        check(!bc::game_profile(L"DeceiveInc-Win64-Shipping.exe", 0x6A966107, 0x05B60000));
        for (const auto &path : {"tests/data/server-startup-mod.json",
                                 "samples/Briefcase.NativeOverlaySample/briefcase.mod.json",
                                 "samples/Briefcase.NativeHello/briefcase.mod.json"}) {
            std::ifstream file(path);
            std::ostringstream text;
            text << file.rdbuf();
            auto manifest = bc::parse_manifest(text.str());
            const bool sample =
                manifest.id == "briefcase.native-overlay-sample";
            check(bc::matches_environment(manifest.environment, Environment::client) ==
                  (sample || manifest.environment == "both"));
            check(bc::matches_environment(manifest.environment, Environment::server) == (!sample));
        }
        bc::ClientStartupSignal startup;
        check(!startup.ready());
        check(!startup.feed("USBShaderPrecompilerSubsytem::StartShaderPrecompilation"));
        check(!startup.feed("Rendered 300 frames; engine initialized"));
        check(!startup.feed("USBShaderPrecompilerSubsytem::Precompile"));
        check(startup.feed("Completed[272] :: Completed!"));
        bc::ClientStartupSignal skipped;
        check(skipped.feed("Showing menu named MENU_LoginScreen"));
        using namespace bc::input;
        for (auto key : {0u, 1u, 2u, 4u, 5u, 6u, 0x1bu, 0x10u, 0x11u, 0x12u, 0x5bu, 0x5cu, 256u, 0xffffffffu})
            check(!valid_menu_key(key));
        for (auto key : {0x70u, 0x75u, 0x87u, 0x41u, 0x2du, 0x6du, 0xe2u}) {
            check(valid_menu_key(key));
            check(menu_key_event(key, key, false, true, false));
            check(!menu_key_event(key, key, true, true, false));
            check(!menu_key_event(key, key, false, false, false));
            check(!menu_key_event(key, key, false, true, true));
        }
        check(!menu_key_event(0x70, 0x75, false, true, false));
        bc::input::Policy input;
        check(!input.open() && !input.capturing());
        input.forwarded(0x57, true);
        check(input.toggle(false, false) && input.capturing());
        check(input.release_on_open(0x57) && !input.release_on_open(0x57));
        check(!input.toggle(true, false) && input.open());
        input.focus(false);
        check(input.open() && !input.capturing());
        check(!input.toggle(false, false));
        input.focus(true);
        check(input.open() && input.capturing());
        check(input.toggle(false, true) && !input.open());
        check(!input.toggle(false, true) && !input.open());
        input.suppress_until_release(0x57, true);
        check(input.suppressed(0x57, true));
        check(!input.suppressed(0x57, false) && !input.suppressed(0x57, true));
        for (auto key : {0x09u, 0x73u})
            check(bc::input::shortcut(key, true, false, false, false));
        check(bc::input::shortcut(0x09, false, true, false, false));
        check(bc::input::shortcut(0x44, false, false, false, true));
        check(!bc::input::shortcut(0x57, false, false, false, false));
        std::cout << "PASS " << checks << " client environment/input contracts\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
