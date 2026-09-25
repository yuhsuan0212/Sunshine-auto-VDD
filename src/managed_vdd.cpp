/** @file src/managed_vdd.cpp
 * @brief Implements the serialized virtual display lease lifecycle.
 */
#include "managed_vdd.h"

#include <algorithm>
#include <stdexcept>

namespace managed_vdd {
  lease_t::lease_t(std::shared_ptr<manager_t> manager, std::uint32_t id, std::uint64_t generation):
      manager_(std::move(manager)),
      id_(id),
      generation_(generation) {}

  lease_t::~lease_t() {
    expire();
  }

  bool lease_t::start() {
    return manager_->start(id_, generation_);
  }

  void lease_t::expire() {
    manager_->release(id_, generation_, true);
  }

  void lease_t::finish() {
    manager_->release(id_, generation_, false);
  }

  manager_t::manager_t(std::unique_ptr<backend_t> backend):
      backend_(std::move(backend)) {
    if (!backend_) {
      throw std::invalid_argument("A managed VDD backend is required");
    }
  }

  bool manager_t::fail(const std::string &message, result_e result) {
    error_ = message;
    state_ = result == result_e::reboot_required ? state_e::reboot_required : state_e::recovery_required;
    return false;
  }

  bool manager_t::cleanup() {
    try {
      state_ = state_e::restoring;
      if (!backend_->restore()) {
        return fail("Baseline restore failed; owned VDD retained");
      }
      state_ = state_e::deactivating;
      const auto result = backend_->deactivate();
      if (result != result_e::ok) {
        return fail("VDD deactivation incomplete", result);
      }
      if (!backend_->wait_inactive()) {
        return fail("VDD is still active or cannot be queried");
      }
      if (!backend_->clear_checkpoint()) {
        return fail("Cannot clear VDD recovery checkpoint");
      }
      state_ = state_e::idle;
      error_.clear();
      return true;
    } catch (const std::exception &e) {
      return fail(e.what());
    }
  }

  bool manager_t::recover() {
    std::lock_guard lock(mutex_);
    if (!leases_.empty()) {
      return false;
    }
    try {
      if (backend_->has_checkpoint()) {
        return cleanup();
      }
      state_ = state_e::idle;
      error_.clear();
      return true;
    } catch (const std::exception &e) {
      return fail(e.what());
    }
  }

  std::shared_ptr<lease_t> manager_t::acquire(std::uint32_t id, mode_t mode) {
    // Allocate before taking the mutex: control-block allocation failure may destroy the lease.
    const auto generation = ++next_generation_;
    auto lease = std::shared_ptr<lease_t>(new lease_t(shared_from_this(), id, generation));
    std::lock_guard lock(mutex_);
    if (leases_.contains(id) || std::any_of(leases_.begin(), leases_.end(), [](const auto &entry) {
          return !entry.second.active;
        })) {
      return {};
    }
    if (state_ != state_e::idle && state_ != state_e::streaming) {
      return {};
    }
    try {
      if (leases_.empty()) {
        if (backend_->has_checkpoint()) {
          fail("Stale VDD checkpoint requires recovery before activation");
          return {};
        }
        if (!backend_->checkpoint()) {
          fail("Cannot save baseline; VDD was not activated");
          return {};
        }
        if ((mode.width || mode.height || mode.fps) && !backend_->prepare_mode(mode)) {
          if (cleanup()) {
            error_ = "Cannot prepare the requested VDD display mode";
          }
          return {};
        }
        state_ = state_e::activating;
        const auto result = backend_->activate();
        if (result == result_e::reboot_required) {
          fail("VDD activation requires a reboot", result);
          return {};
        }
        if (result != result_e::ok || !backend_->wait_ready()) {
          cleanup();
          return {};
        }
      }
      leases_.emplace(id, entry_t {false, generation});
      state_ = std::any_of(leases_.begin(), leases_.end(), [](const auto &entry) {
        return entry.second.active;
      }) ?
                 state_e::streaming :
                 state_e::pending;
      return lease;
    } catch (const std::exception &e) {
      fail(e.what());
      return {};
    }
  }

  bool manager_t::start(std::uint32_t id, std::uint64_t generation) {
    std::lock_guard lock(mutex_);
    const auto it = leases_.find(id);
    if (it == leases_.end() || it->second.active || it->second.generation != generation) {
      return false;
    }
    it->second.active = true;
    state_ = state_e::streaming;
    return true;
  }

  void manager_t::release(std::uint32_t id, std::uint64_t generation, bool pending_only) {
    std::lock_guard lock(mutex_);
    const auto it = leases_.find(id);
    if (it == leases_.end() || it->second.generation != generation || (pending_only && it->second.active)) {
      return;
    }
    leases_.erase(it);
    if (leases_.empty()) {
      cleanup();
    } else {
      state_ = std::any_of(leases_.begin(), leases_.end(), [](const auto &entry) {
        return entry.second.active;
      }) ?
                 state_e::streaming :
                 state_e::pending;
    }
  }

  state_e manager_t::state() const {
    std::lock_guard lock(mutex_);
    return state_;
  }

  std::string manager_t::error() const {
    std::lock_guard lock(mutex_);
    return error_;
  }
}  // namespace managed_vdd
