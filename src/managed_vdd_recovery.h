/**
 * @file src/managed_vdd_recovery.h
 * @brief Recovery of an abandoned display transaction while the host is quiescent.
 */
#pragma once

#include "managed_vdd.h"

namespace managed_vdd {
  /** @brief Whether the process recorded before display mutation still exists. */
  enum class owner_state_e {
    alive,  ///< The exact PID and creation time still identify a live host.
    abandoned,  ///< The recorded process has exited.
    unknown  ///< Missing metadata or an inaccessible process; never assume abandoned.
  };

  /** @brief Outcome reported to the recovery supervisor. */
  enum class recovery_result_e {
    idle,  ///< There is no unfinished transaction.
    busy,  ///< A live or unidentifiable owner prevents automatic recovery.
    restored,  ///< The original desktop and inactive VDD were verified.
    rescued,  ///< A physical output was recovered; the old layout is archived.
    failed  ///< Recovery remains incomplete and must be retried.
  };

  /** @brief Host coordination performed while the backend's cross-process lock is held. */
  class recovery_host_t {
  public:
    virtual ~recovery_host_t() = default;
    /** @brief Query the exact process saved in the journal, without changing state. */
    virtual owner_state_e owner_state() = 0;
    /** @brief Stop the managed host and verify that no matching host can still write display state. */
    virtual bool quiesce() = 0;
    /** @brief Activate and verify an attached physical display when exact restoration is impossible. */
    virtual bool rescue_physical() = 0;
    /** @brief Verify the recovered desktop still exists after disabling the VDD. */
    virtual bool verify_desktop(bool rescued) = 0;
    /** @brief Archive recovery evidence and retire the host's obsolete display persistence. */
    virtual bool retire_state(bool rescued) = 0;
    /** @brief Resume only a service this recovery stopped; called even after a failed quiesce. */
    virtual bool resume() = 0;
  };

  /**
   * @brief Recover only abandoned transactions, with no live display writer.
   * @param backend Locked device backend holding the durable baseline.
   * @param host Service coordination and last-resort physical recovery.
   * @return Verified recovery outcome; failures retain the checkpoint where possible.
   */
  recovery_result_e recover_abandoned(backend_t &backend, recovery_host_t &host);
}  // namespace managed_vdd
