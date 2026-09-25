/** @file src/platform/windows/managed_vdd.h
 * @brief Owned PnP toggle provider and durable display recovery for the VDD prototype.
 */
#pragma once
#include "src/managed_vdd.h"

#include <filesystem>
#include <functional>

namespace managed_vdd {
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
    /** @brief Activate the owned target, make it primary and apply the first client's mode. */
    bool configure(int width, int height, int fps, bool hdr);
    /** @brief Return read-only PnP and display diagnostics as JSON. */
    std::string inspect() const;
    /** @brief Provision one new owned disabled device from an explicitly supplied signed INF. */
    static std::string provision(const std::filesystem::path &inf, const std::filesystem::path &owner_file);

  private:
    struct impl_t;
    std::unique_ptr<impl_t> impl_;  ///< Encapsulates Windows APIs and the recovery journal.
  };
}  // namespace managed_vdd
