/**
 * @file src/managed_vdd_recovery.cpp
 * @brief Serialize orphan recovery with host shutdown and obsolete-state retirement.
 */
#include "managed_vdd_recovery.h"

namespace managed_vdd {
  recovery_result_e recover_abandoned(backend_t &backend, recovery_host_t &host) {
    if (!backend.has_checkpoint()) {
      return recovery_result_e::idle;
    }
    if (host.owner_state() != owner_state_e::abandoned) {
      return recovery_result_e::busy;
    }
    auto result = recovery_result_e::failed;
    // quiesce may partially stop a service before failing. Always let the host resume it.
    try {
      if (host.quiesce()) {
        bool exact = false;
        try {
          exact = backend.restore();
        } catch (...) {
          // A partially applied old layout still permits a verified physical rescue.
        }
        if ((exact || host.rescue_physical()) && backend.deactivate() == result_e::ok && backend.wait_inactive() && host.verify_desktop(!exact) && host.retire_state(!exact) && backend.clear_checkpoint()) {
          result = exact ? recovery_result_e::restored : recovery_result_e::rescued;
        }
      }
    } catch (...) {
      // Keep the journal and resume the service; the next supervisor run can retry.
    }
    try {
      if (!host.resume()) {
        return recovery_result_e::failed;
      }
    } catch (...) {
      return recovery_result_e::failed;
    }
    return result;
  }
}  // namespace managed_vdd
