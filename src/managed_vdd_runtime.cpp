/** @file src/managed_vdd_runtime.cpp
 * @brief Bridges the platform-independent lease manager into Sunshine.
 */
#include "managed_vdd_runtime.h"

#include "config.h"
#include "logging.h"
#include "rtsp.h"

#include <stdexcept>
#ifdef _WIN32
  #include "platform/windows/managed_vdd.h"
#endif

namespace managed_vdd::runtime {
  namespace {
    std::shared_ptr<manager_t> manager;  ///< Initialized before the HTTP and RTSP threads start.
#ifdef _WIN32
    windows_backend_t *backend = nullptr;  ///< Owned by manager for the entire process lifetime.
#endif
  }  // namespace

  bool enabled() {
    return !config::video.managed_vdd_owner_file.empty();
  }

  void init(const std::filesystem::path &owner_file) {
    if (owner_file.empty()) {
      return;
    }
    try {
#ifdef _WIN32
      auto implementation = std::make_unique<windows_backend_t>(owner_file);
      backend = implementation.get();
      manager = std::make_shared<manager_t>(std::move(implementation));
      if (!manager->recover()) {
        BOOST_LOG(error) << "Managed VDD recovery: " << manager->error();
      } else {
        BOOST_LOG(info) << "Managed VDD ready: owned instance verified, idle recovery complete";
      }
#else
      BOOST_LOG(error) << "Managed VDD is supported only on Windows";
#endif
    } catch (const std::exception &e) {
      BOOST_LOG(error) << "Managed VDD initialization failed: " << e.what();
    }
  }

  std::shared_ptr<lease_t> prepare(const rtsp_stream::launch_session_t &session) {
    if (!enabled()) {
      return {};
    }
    if (!manager) {
      throw std::runtime_error("Managed VDD is configured but initialization failed");
    }
    auto lease = manager->acquire(session.id, {session.width, session.height, session.fps});
    if (!lease) {
      throw std::runtime_error("Managed VDD busy or recovery required: " + manager->error());
    }
#ifdef _WIN32
    if (manager->state() == state_e::pending) {
      const auto mode = std::to_string(session.width) + "x" + std::to_string(session.height) + " at " + std::to_string(session.fps) + " Hz, HDR " + (session.enable_hdr ? "on" : "off");
      BOOST_LOG(info) << "Managed VDD requested mode: " << mode;
      if (!backend->configure(session.width, session.height, session.fps, session.enable_hdr)) {
        lease->expire();
        BOOST_LOG(error) << "Managed VDD could not apply " << mode << "; recovery: " << (manager->state() == state_e::idle ? "complete" : manager->error());
        throw std::runtime_error("Managed VDD cannot apply " + mode + ". Windows did not advertise or accept the requested mode.");
      }
    }
#endif
    return lease;
  }

  std::string output_name() {
#ifdef _WIN32
    if (enabled() && backend) {
      return backend->device_id();
    }
#endif
    return {};
  }

  void shutdown() {
    if (manager && !manager->recover()) {
      BOOST_LOG(error) << "Managed VDD shutdown recovery: " << manager->error();
    }
  }
}  // namespace managed_vdd::runtime
