#include "../runtime/Briefcase.UnrealBackend/ObjectDeleteListener.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace RC::Unreal;
static std::vector<FUObjectDeleteListener *> listeners;
static unsigned deleted{}, shutdowns{}, removals{};
static bool empty_during_cleanup{};
static const UObjectBase *received{};
static int32_t received_index{};
// Test double for Unreal's listener array. The production listener itself is used.
void UObjectArray::AddUObjectDeleteListener(FUObjectDeleteListener *listener) {
    listeners.push_back(listener);
}
void UObjectArray::RemoveUObjectDeleteListener(FUObjectDeleteListener *listener) {
    const auto before = listeners.size();
    std::erase(listeners, listener);
    removals += unsigned(before - listeners.size());
}
static void deleted_callback(const UObjectBase *object, int32_t index) noexcept {
    ++deleted;
    received = object;
    received_index = index;
}
static void cleanup() noexcept {
    ++shutdowns;
    empty_during_cleanup = listeners.empty();
}
int main() {
    try {
        bc::ObjectDeleteListener listener(deleted_callback, cleanup);
        UObjectArray::AddUObjectDeleteListener(&listener);
        const auto *object = reinterpret_cast<const UObjectBase *>(uintptr_t(0x1000));
        listeners.front()->NotifyUObjectDeleted(object, 42);
        if (deleted != 1 || received != object || received_index != 42)
            throw std::runtime_error("deletion notification forwarding");
        // Reproduces the game's shutdown broadcast followed by its fatal empty-list assertion.
        const auto snapshot = listeners;
        for (auto *registered : snapshot)
            registered->OnUObjectArrayShutdown();
        if (!listeners.empty())
            throw std::runtime_error(
                "All UObject delete listeners should be unregistered when shutting down the UObject array");
        if (removals != 1 || shutdowns != 1 || !empty_during_cleanup)
            throw std::runtime_error("unregister before owner cleanup");
        // Mod cleanup may have run earlier: array teardown must still remove the engine listener.
        shutdowns = 10;
        empty_during_cleanup = false;
        UObjectArray::AddUObjectDeleteListener(&listener);
        listeners.front()->OnUObjectArrayShutdown();
        if (!listeners.empty() || removals != 2 || shutdowns != 11 || !empty_during_cleanup)
            throw std::runtime_error("already-stopped host still unregisters listener");
        std::cout << "PASS UObject array shutdown listener regression (production observer)\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
