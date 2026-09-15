#pragma once
#include "ModApi.h"
#include <utility>

namespace briefcase {
// An owned ABI reference, not a UObject pointer. Move-only, like a scoped IDisposable.
// Adopt only a reference already owned by this mod. Destroy/reset on the game thread.
class ObjectHandle {
    const BcApi *api_{};
    BcHandle handle_{};

    ObjectHandle(const BcApi *api, BcHandle handle) noexcept : api_(api), handle_(handle) {}

  public:
    ObjectHandle() = default;
    static ObjectHandle Adopt(const BcApi *api, BcHandle owned) noexcept { return {api, owned}; }
    ObjectHandle(const ObjectHandle &) = delete;
    ObjectHandle &operator=(const ObjectHandle &) = delete;
    ObjectHandle(ObjectHandle &&other) noexcept
        : api_(other.api_), handle_(std::exchange(other.handle_, 0)) {}
    ObjectHandle &operator=(ObjectHandle &&other) noexcept {
        if (this != &other) {
            Reset();
            api_ = other.api_;
            handle_ = std::exchange(other.handle_, 0);
        }
        return *this;
    }
    ~ObjectHandle() { Reset(); }
    void Reset() noexcept {
        if (auto handle = std::exchange(handle_, 0))
            api_->release_handle(api_->context, handle);
    }
    BcHandle Handle() const noexcept { return handle_; }
    explicit operator bool() const noexcept { return handle_ != 0; }
    bool IsValid() const { return handle_ && api_->validate_handle(api_->context, handle_) == BC_OK; }
};
} // namespace briefcase
