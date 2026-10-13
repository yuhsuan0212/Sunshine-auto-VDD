/**
 * @file src/platform/windows/managed_vdd.h
 * @brief Control an owned virtual display device and record the displays it replaces.
 */
#pragma once
#include "src/managed_vdd.h"
#include "src/managed_vdd_recovery.h"

#include <display_device/types.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace managed_vdd {
  /**
   * @brief Check an exact process identity without confusing a reused PID with its old owner.
   * @param pid Recorded process ID.
   * @param created Recorded Windows process creation FILETIME as an unsigned integer.
   * @return Alive, abandoned, or unknown when access or metadata is insufficient.
   */
  owner_state_e query_process_owner(uint32_t pid, uint64_t created);

  /** @brief What an active display path currently leads to. */
  enum class target_e {
    monitor,  ///< The target name query returned a monitor device path, and the display could be identified.
    no_monitor,  ///< The query succeeded with an empty monitor device path, as seen for a path left behind by an unplugged monitor.
    unknown  ///< The target could not be queried or identified.
  };

  /** @brief One active display path, as seen when a baseline is recorded. */
  struct active_target_t {
    target_e state;  ///< What the path leads to.
    std::string device_id;  ///< Display ID used in the topology; only set for a monitor.
  };

  /**
   * @brief Decide whether the active paths show a machine without any display.
   *
   * A disconnected monitor can leave active paths behind, so paths only count as headless when
   * none of them reports a monitor device path. This was verified with an unplugged DisplayPort
   * monitor; Windows does not document it as a guarantee. An empty topology alone is not enough,
   * because identifying a display can fail transiently.
   * @param active_targets State of each active path, or nullptr when the path query failed.
   * @param no_active_paths Whether Windows separately reported zero active paths.
   * @return True for zero active paths or paths that all lead to no monitor.
   */
  bool is_headless(const std::vector<active_target_t> *active_targets, bool no_active_paths);

  /**
   * @brief Check that a recorded topology holds exactly the monitors seen on the active paths.
   *
   * Identifying a display can fail transiently and silently drop it from the topology, and the
   * topology comes from a later query, so a hot-plugged display could take its place. Comparing
   * identities rather than counts catches both.
   * @param active_targets Each active path from the first query.
   * @param identified Display IDs in the recorded topology.
   * @return False if any target could not be identified or the sets of displays differ.
   */
  bool is_complete_baseline(const std::vector<active_target_t> &active_targets, const display_device::StringSet &identified);

  /** @brief Windows backend. Construction and inspection never change device state. */
  class windows_backend_t final: public backend_t {
  public:
    /** @brief Load an ownership file created by the provisioning tool. */
    explicit windows_backend_t(const std::filesystem::path &owner_file);
    /** @brief Release in-process resources without deactivating displays. */
    ~windows_backend_t() override;
    bool has_checkpoint() override;
    bool checkpoint() override;
    bool prepare_mode(mode_t mode) override;
    result_e activate() override;
    bool wait_ready() override;
    bool restore() override;
    result_e deactivate() override;
    bool wait_inactive() override;
    bool clear_checkpoint() override;
    /** @brief Return the currently enumerated owned display's stable libdisplaydevice ID. */
    std::string device_id() const;
    /** @brief Return read-only PnP and display diagnostics as JSON. */
    std::string inspect() const;
    /** @brief Reject a host trying to reuse a different process's unfinished transaction. */
    void require_matching_host() const;
    /**
     * @brief Recover an orphan on the interactive console, with the Sunshine service stopped.
     * @param service Name of the installed Sunshine service.
     * @param state_file Sunshine's display_device.state file to archive after verified recovery.
     * @return Recovery outcome. Unknown owners and non-console sessions are left untouched.
     */
    recovery_result_e recover(const std::wstring &service, const std::filesystem::path &state_file);
    /** @brief Provision one new owned disabled device from an explicitly supplied signed INF. */
    static std::string provision(const std::filesystem::path &inf, const std::filesystem::path &owner_file);

  private:
    struct impl_t;
    std::unique_ptr<impl_t> impl_;  ///< Encapsulates Windows APIs and the recovery journal.
  };
}  // namespace managed_vdd
