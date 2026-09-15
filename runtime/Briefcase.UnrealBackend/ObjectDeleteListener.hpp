#pragma once
#include <Unreal/UObjectArray.hpp>
namespace bc {
// Unreal checks that this list is empty after broadcasting OnUObjectArrayShutdown.
// The listener must remove itself during that notification, even when mods were
// already stopped by the host's control plane.
class ObjectDeleteListener final : public RC::Unreal::FUObjectDeleteListener {
  public:
    using Deleted = void (*)(const RC::Unreal::UObjectBase *, int32_t) noexcept;
    using Shutdown = void (*)() noexcept;
    ObjectDeleteListener(Deleted deleted, Shutdown shutdown) : deleted_(deleted), shutdown_(shutdown) {}
    void NotifyUObjectDeleted(const RC::Unreal::UObjectBase *object, int32_t index) override {
        deleted_(object, index);
    }
    void OnUObjectArrayShutdown() override {
        RC::Unreal::UObjectArray::RemoveUObjectDeleteListener(this);
        shutdown_();
    }

  private:
    Deleted deleted_;
    Shutdown shutdown_;
};
} // namespace bc
