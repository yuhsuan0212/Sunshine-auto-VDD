/** @file src/managed_vdd_runtime.h
 * @brief Sunshine integration for the opt-in managed VDD prototype.
 */
#pragma once
#include "managed_vdd.h"

#include <filesystem>

namespace rtsp_stream {
  struct launch_session_t;
}

namespace managed_vdd::runtime {
  /** @brief Return whether VDD management was explicitly configured. */
  bool enabled();
  /** @brief Initialize and recover the owned instance before accepting launches. */
  void init(const std::filesystem::path &owner_file);
  /** @brief Acquire a lease and prepare the first client's display before probing encoders. */
  std::shared_ptr<lease_t> prepare(const rtsp_stream::launch_session_t &session);
  /** @brief Return the managed capture selector, or empty if no display is leased. */
  std::string output_name();
  /** @brief Retry safe cleanup after all server threads and capture workers have stopped. */
  void shutdown();
}  // namespace managed_vdd::runtime
