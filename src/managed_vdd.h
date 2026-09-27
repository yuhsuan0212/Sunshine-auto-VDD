/**
 * @file src/managed_vdd.h
 * @brief Enable an owned virtual display for a stream and disable it afterwards.
 */
#pragma once

#include "vdd_modes.h"

namespace managed_vdd {
  /** @brief Result of an operation on the owned device. */
  enum class result_e {
    ok,  ///< Operation completed and can be verified.
    failed,  ///< Operation did not complete.
    reboot_required  ///< Windows requires a restart.
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

  /**
   * @brief Enable the owned VDD for a stream (a pre-display Do command).
   *
   * A previous activation whose Undo never ran is cleaned up first.
   * @param backend Platform operations.
   * @param mode Mode to make available before the device starts.
   * @return True if the VDD is active.
   */
  bool activate_for_stream(backend_t &backend, mode_t mode);

  /**
   * @brief Restore the saved baseline, then disable the VDD (a pre-display Undo command).
   *
   * Nothing is changed when the baseline cannot be restored, so the device and its
   * checkpoint remain for recovery. Repeating the call after success is harmless.
   * @param backend Platform operations.
   * @return True if the VDD is inactive and no checkpoint remains.
   */
  bool deactivate_after_stream(backend_t &backend);
}  // namespace managed_vdd
