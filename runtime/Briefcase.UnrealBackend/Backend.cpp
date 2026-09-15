#include <Windows.h>

#include "Backend.hpp"
#include "ClientConnection.hpp"
#include "HandleTable.hpp"
#include "HookRegistry.hpp"
#include "NativeContract.hpp"
#include "ObjectDeleteListener.hpp"
#include "PropertyPath.hpp"
#include "ReflectionContract.hpp"
#include "UnrealServices.hpp"
#include <DynamicOutput/Output.hpp>
#include <MinHook.h>
#include <Unreal/FProperty.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FObjectProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UEnum.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/UnrealInitializer.hpp>
#include <Unreal/UnrealVersion.hpp>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <set>
#include <tuple>
#include <unordered_set>

#include <atomic>
#include <chrono>
#include <deque>
#include <format>
#include <mutex>
#include <unordered_map>
namespace bc {
using namespace RC::Unreal;
static DWORD game_thread_id;
static std::atomic<bool> ready{}, pending{};
static std::atomic<int> init_state{};
static std::atomic<double> measured_initialization_ms{};
static ULONGLONG initialize_after;
static uint64_t sleep_original{};

static thread_local bool pumping{};
static std::mutex queue_mutex;
struct Task {
    BcTask callback;
    void *user;
    uint64_t owner;
};
static std::deque<Task> queue;
static HandleTable<UObject> handles;
static std::unordered_set<uint64_t> disabled_owners;
static std::mutex deleted_mutex;
static std::vector<HandleTable<UObject>::Revoked> deleted_queue;
static std::atomic<bool> deleted_pending{};
static std::atomic<BcTask> shutdown_handler{};
static std::atomic<void *> shutdown_user{};
static void dispatch_deleted(const std::vector<HandleTable<UObject>::Revoked> &) noexcept;
static void reflected_event_pre(UObject *, UFunction *, void *) noexcept;
static void reflected_event_post(UObject *, UFunction *, void *) noexcept;
static thread_local unsigned dispatch_depth;
void backend_set_shutdown_handler(BcTask callback, void *user) {
    shutdown_user = user;
    shutdown_handler = callback;
}

static void object_deleted(const UObjectBase *object, int32_t) noexcept {
    try {
        auto revoked = handles.invalidate_collect(object);
        if (revoked.empty())
            return;
        std::lock_guard lock(deleted_mutex);
        deleted_queue.insert(deleted_queue.end(), revoked.begin(), revoked.end());
        deleted_pending.store(true, std::memory_order_release);
    } catch (...) {
        log("ERROR: object deletion notification failed");
    }
}
static void object_array_shutdown() noexcept {
    log("UObject delete listener unregistered during array shutdown");
    if (auto callback = shutdown_handler.load(std::memory_order_acquire);
        GetCurrentThreadId() == game_thread_id && callback) {
        try {
            callback(shutdown_user);
        } catch (...) {
            log("ERROR: shutdown callback threw");
        }
    }
    handles.clear();
    ready = false;
}
static ObjectDeleteListener delete_listener{object_deleted, object_array_shutdown};
class LogDevice final : public RC::Output::OutputDevice {
    void receive(RC::File::StringViewType message) const override {
        int size = WideCharToMultiByte(CP_UTF8, 0, message.data(), static_cast<int>(message.size()), nullptr,
                                       0, nullptr, nullptr);
        std::string text(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, message.data(), static_cast<int>(message.size()), text.data(), size,
                            nullptr, nullptr);
        bc::log("UE4SS: " + text);
    }
};
static void pump() noexcept {
    if (GetCurrentThreadId() != game_thread_id || pumping || dispatch_depth ||
        !ready.load(std::memory_order_acquire) ||
        (!pending.load(std::memory_order_acquire) && !deleted_pending.load(std::memory_order_acquire)))
        return;
    pumping = true;
    if (deleted_pending.load(std::memory_order_acquire)) {
        std::vector<HandleTable<UObject>::Revoked> events;
        {
            std::lock_guard lock(deleted_mutex);
            events.swap(deleted_queue);
            deleted_pending = false;
        }
        dispatch_deleted(events);
    }
    std::deque<Task> tasks;
    {
        std::lock_guard lock(queue_mutex);
        for (size_t i = 0; i < 32 && !queue.empty(); ++i) {
            tasks.push_back(queue.front());
            queue.pop_front();
        }
        pending.store(!queue.empty(), std::memory_order_release);
    }
    for (auto task : tasks) {
        {
            std::lock_guard lock(queue_mutex);
            if (disabled_owners.contains(task.owner))
                continue;
        }
        try {
            task.callback(task.user);
        } catch (...) {
            log("ERROR: mod callback threw across the ABI");
        }
    }
    pumping = false;
}
static void initialize_on_game_thread() noexcept {
    pumping = true;
    const auto start = std::chrono::steady_clock::now();
    try {
        log(std::format("Backend initialization on game thread {}", GetCurrentThreadId()));
        RC::Output::DefaultTargets::get_default_devices_ref().push_back(std::make_unique<LogDevice>());
        Hook::RegisterProcessEventPreCallback(reflected_event_pre);
        Hook::RegisterProcessEventPostCallback([](UObject *self, UFunction *function, void *parameters) {
            reflected_event_post(self, function, parameters);
            pump();
        });
        UnrealInitializer::Config config{};
        config.SecondsToScanBeforeGivingUp = 10;
        config.bUseUObjectArrayCache = false;
        config.bEnableCache = false;
        UnrealInitializer::Initialize(config);
        if (Version::Major != 4 || Version::Minor != 27)
            throw std::runtime_error("UE4SS detected an unsupported Unreal version");
        UObjectArray::AddUObjectDeleteListener(&delete_listener);
        ready.store(true, std::memory_order_release);
        measured_initialization_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        init_state = 2;
        log(std::format(
            "Backend ready; Unreal {}.{}; initialization_ms={}", Version::Major, Version::Minor,
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
                .count()));
    } catch (const std::exception &e) {
        log(std::string("ERROR backend: ") + e.what());
        init_state = 3;
    } catch (...) {
        log("ERROR backend: unknown initialization failure");
        init_state = 3;
    }
    pumping = false;
}
static void WINAPI hooked_sleep(DWORD ms) {
    if (GetCurrentThreadId() == game_thread_id && !pumping) {
        int expected = 0;
        if (GetTickCount64() >= initialize_after && init_state.compare_exchange_strong(expected, 1))
            initialize_on_game_thread();
        pump();
    }
    reinterpret_cast<void(WINAPI *)(DWORD)>(sleep_original)(ms);
}
void backend_start(uint32_t thread, uint32_t delay_ms) {
    game_thread_id = thread;
    initialize_after = GetTickCount64() + delay_ms;
    // Patch only the dedicated executable's Sleep import. No system DLL detour.
    auto *base = reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr));
    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
    const auto &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress || !dir.Size)
        throw std::runtime_error("Missing game import table");
    auto *imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress);
    ULONG_PTR *slot = nullptr;
    for (size_t i = 0; i < dir.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR) && imports[i].Name; ++i) {
        if (_stricmp(reinterpret_cast<const char *>(base + imports[i].Name), "KERNEL32.dll") != 0)
            continue;
        if (!imports[i].OriginalFirstThunk)
            continue;
        auto *names = reinterpret_cast<IMAGE_THUNK_DATA64 *>(base + imports[i].OriginalFirstThunk);
        auto *addresses = reinterpret_cast<IMAGE_THUNK_DATA64 *>(base + imports[i].FirstThunk);
        for (size_t j = 0; names[j].u1.AddressOfData; ++j) {
            if (IMAGE_SNAP_BY_ORDINAL64(names[j].u1.Ordinal))
                continue;
            auto *name = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(base + names[j].u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char *>(name->Name), "Sleep") == 0) {
                slot = &addresses[j].u1.Function;
                break;
            }
        }
    }
    if (!slot)
        throw std::runtime_error("Dedicated executable has no Sleep import");
    DWORD old_protection{};
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old_protection))
        throw std::runtime_error("Cannot protect Sleep import slot");
    sleep_original = *slot;
    InterlockedExchangePointer(reinterpret_cast<void *volatile *>(slot),
                               reinterpret_cast<void *>(&hooked_sleep));
    DWORD ignored{};
    VirtualProtect(slot, sizeof(*slot), old_protection, &ignored);
    log(std::format("Game-thread IAT bootstrap armed for thread {}; delay {} ms", thread, delay_ms));
}

uint32_t backend_status() noexcept {
    return uint32_t(init_state.load());
}
double backend_initialization_ms() noexcept {
    return measured_initialization_ms.load();
}
bool backend_ready() noexcept {
    return ready.load(std::memory_order_acquire);
}
static BcResult thread_check() {
    if (GetCurrentThreadId() != game_thread_id)
        return BC_WRONG_THREAD;
    return backend_ready() ? BC_OK : BC_NOT_READY;
}
BcResult backend_find(uint64_t owner, std::string_view path, BcHandle *out) {
    if (auto error = thread_check(); error != BC_OK)
        return error;
    if (!out || path.empty() || path.find('\0') != path.npos)
        return BC_INVALID_ARGUMENT;
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()),
                                   nullptr, 0);
    if (!size)
        return BC_INVALID_ARGUMENT;
    std::wstring wide(size, '\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()),
                        wide.data(), size);
    auto *object = UObjectGlobals::StaticFindObject<UObject *>(nullptr, nullptr, wide.c_str());
    if (!object)
        return BC_NOT_FOUND;
    if (!UObjectArray::IsValid(object->GetObjectItem(), false))
        return BC_STALE_HANDLE;
    auto token = handles.insert(owner, object);
    if (!token)
        return BC_LIMIT;
    *out = *token;
    return BC_OK;
}
BcResult backend_validate(uint64_t owner, BcHandle handle) {
    if (auto error = thread_check(); error != BC_OK)
        return error;
    return handles.valid(
               owner, handle,
               [](UObject *object) { return UObjectArray::IsValid(object->GetObjectItem(), false); })
               ? BC_OK
               : BC_STALE_HANDLE;
}
BcResult backend_release(uint64_t owner, BcHandle handle) {
    if (auto error = thread_check(); error != BC_OK)
        return error;
    return handles.release(owner, handle) ? BC_OK : BC_STALE_HANDLE;
}
BcResult backend_post(BcTask task, void *user, uint64_t owner) {
    if (!task)
        return BC_INVALID_ARGUMENT;
    if (!backend_ready())
        return BC_NOT_READY;
    std::lock_guard lock(queue_mutex);
    if (queue.size() >= 1024)
        return BC_LIMIT;
    if (disabled_owners.contains(owner))
        return BC_DENIED;
    queue.push_back({task, user, owner});
    pending.store(true, std::memory_order_release);
    return BC_OK;
}
// clang-format off: private implementation fragments depend on this order.
#include "Reflection.inc"
#include "ClientConnection.inc"
#include "HookBridge.inc"
#include "NativeDetours.inc"
// clang-format on
} // namespace bc
