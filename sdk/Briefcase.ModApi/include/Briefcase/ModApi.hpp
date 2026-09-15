#pragma once
#include "ModApi.h"
#include <string_view>
namespace briefcase {
class Api {
    const BcApi *api_;

  public:
    explicit Api(const BcApi *api) : api_(api) {}
    BcResult log(std::string_view message) const noexcept {
        return api_->log(api_->context, 1, message.data(), static_cast<uint32_t>(message.size()));
    }
    BcResult find(std::string_view path, BcHandle &handle) const noexcept {
        return api_->find_object(api_->context, path.data(), static_cast<uint32_t>(path.size()), &handle);
    }
    BcResult validate(BcHandle handle) const noexcept { return api_->validate_handle(api_->context, handle); }
    BcResult release(BcHandle handle) const noexcept { return api_->release_handle(api_->context, handle); }
    BcResult post(BcTask task, void *user) const noexcept {
        return api_->post_game_thread(api_->context, task, user);
    }
};
} // namespace briefcase
