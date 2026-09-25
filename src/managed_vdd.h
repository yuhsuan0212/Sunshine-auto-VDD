/**
 * @file src/managed_vdd.h
 * @brief Transactional ownership of a virtual display across pending and active streams.
 */
#pragma once

#include "vdd_modes.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace managed_vdd {
  /** @brief Result of an operation on the owned device. */
  enum class result_e {
    ok,  ///< Operation completed and can be verified.
    failed,  ///< Operation did not complete.
    reboot_required  ///< Windows requires a restart.
  };
  /** @brief Observable lifecycle; failed cleanup never reports idle. */
  enum class state_e {
    idle,  ///< No display lease or recovery checkpoint.
    activating,  ///< Enabling the owned device.
    pending,  ///< Waiting for capture startup.
    streaming,  ///< Capture holds at least one lease.
    restoring,  ///< Returning to the recorded baseline.
    deactivating,  ///< Disabling and verifying the owned target.
    recovery_required,  ///< Cleanup is incomplete; retain the journal.
    reboot_required  ///< PnP requires a restart.
  };

  /** @brief Platform operations; implementations must bound waits and verify postconditions. */
  class backend_t {
  public:
    /** @brief Destroy the backend without changing device state. */
    virtual ~backend_t() = default;
    /** @brief Check for a durable recovery checkpoint, including malformed checkpoints. */
    virtual bool has_checkpoint() = 0;
    /** @brief Persist the original topology before making any changes. */
    virtual bool checkpoint() = 0;
    /** @brief Make the first client's mode available before loading the driver. */
    virtual bool prepare_mode(mode_t mode) = 0;
    /** @brief Activate only the owned instance and verify PnP state. */
    virtual result_e activate() = 0;
    /** @brief Wait for the owned display target to become available. */
    virtual bool wait_ready() = 0;
    /** @brief Restore the saved baseline; a headless baseline may defer zero-output state to deactivation. */
    virtual bool restore() = 0;
    /** @brief Deactivate only the owned device; an already inactive device is success. */
    virtual result_e deactivate() = 0;
    /** @brief Verify inactive PnP state and absence of the owned active display target. */
    virtual bool wait_inactive() = 0;
    /** @brief Remove the checkpoint only after successful cleanup. */
    virtual bool clear_checkpoint() = 0;
  };

  class manager_t;

  /** @brief Shared lease that survives HTTP handoff and is explicitly released after capture joins. */
  class lease_t {
  public:
    /** @brief Release a forgotten pending lease when its final owner disappears. */
    ~lease_t();
    /** @brief Promote a pending lease before starting capture. @return False if it expired. */
    bool start();
    /** @brief Expire only a pending lease; cannot tear down a running stream. */
    void expire();
    /** @brief Release after capture stops; repeated calls are harmless. */
    void finish();
    lease_t(const lease_t &) = delete;
    lease_t &operator=(const lease_t &) = delete;

  private:
    friend class manager_t;
    /** @brief Construct a lease owned by its manager. */
    lease_t(std::shared_ptr<manager_t> manager, std::uint32_t id, std::uint64_t generation);
    std::shared_ptr<manager_t> manager_;  ///< Keeps recovery operations alive until the last lease.
    std::uint32_t id_;  ///< Launch ID; cannot be recycled while a lease is outstanding.
    std::uint64_t generation_;  ///< Prevents delayed release from affecting a reused launch ID.
  };

  /** @brief Serializes activation, pending handshakes, active streams and recovery. */
  class manager_t: public std::enable_shared_from_this<manager_t> {
  public:
    /** @brief Construct a manager with platform operations. */
    explicit manager_t(std::unique_ptr<backend_t> backend);
    /** @brief Recover a stale checkpoint only when no sessions hold leases. @return Recovery succeeded. */
    bool recover();
    /** @brief Acquire a pending launch, rejecting a second concurrent pending handshake. */
    std::shared_ptr<lease_t> acquire(std::uint32_t id, mode_t mode = {});
    /** @brief Return the current lifecycle state. */
    state_e state() const;
    /** @brief Return the last failure description. */
    std::string error() const;

  private:
    friend class lease_t;
    /** @brief Promote a pending launch while holding the lifecycle mutex. */
    bool start(std::uint32_t id, std::uint64_t generation);
    /** @brief Release a launch; pending_only protects running streams from handshake timers. */
    void release(std::uint32_t id, std::uint64_t generation, bool pending_only);
    /** @brief Restore and deactivate; caller holds the mutex. */
    bool cleanup();
    /** @brief Record a failure and retain the checkpoint. */
    bool fail(const std::string &message, result_e result = result_e::failed);
    mutable std::mutex mutex_;  ///< Serializes lifecycle transitions, including backend operations.
    std::unique_ptr<backend_t> backend_;  ///< Device and topology operations.

    /** @brief Records a launch's phase and unique generation. */
    struct entry_t {
      bool active;  ///< True while capture owns the lease.
      std::uint64_t generation;  ///< Unique acquisition identity.
    };

    std::map<std::uint32_t, entry_t> leases_;  ///< Pending and active leases.
    std::atomic<std::uint64_t> next_generation_ = 0;  ///< Monotonic acquisition counter.
    state_e state_ {state_e::idle};  ///< Current state.
    std::string error_;  ///< Most recent operation failure.
  };
}  // namespace managed_vdd
