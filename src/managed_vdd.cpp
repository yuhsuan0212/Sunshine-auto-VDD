/**
 * @file src/managed_vdd.cpp
 * @brief Implements the VDD activation and deactivation sequences.
 */
#include "managed_vdd.h"

#include <tuple>

namespace managed_vdd {
  bool activate_for_stream(backend_t &backend, mode_t mode) {
    // An existing checkpoint means a previous Undo never ran.
    if (!backend.checkpoint() && (!deactivate_after_stream(backend) || !backend.checkpoint())) {
      return false;
    }
    if (backend.prepare_mode(mode) && backend.activate() == result_e::ok && backend.wait_ready()) {
      return true;
    }
    std::ignore = deactivate_after_stream(backend);
    return false;
  }

  bool deactivate_after_stream(backend_t &backend) {
    // Sunshine normally restored the displays already; if not, the VDD may be the only output.
    if (backend.has_checkpoint() && !backend.restore()) {
      return false;
    }
    return backend.deactivate() == result_e::ok && backend.wait_inactive() && backend.clear_checkpoint();
  }
}  // namespace managed_vdd
