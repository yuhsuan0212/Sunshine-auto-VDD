/** @file tools/managed-vdd-poc/main.cpp
 * @brief Explicitly invoked provisioning, diagnostics and smoke cycles for managed VDD.
 */
#include "src/platform/windows/managed_vdd.h"

#include <display_device/json.h>
#include <display_device/windows/win_api_layer.h>
#include <display_device/windows/win_display_device.h>
#include <iostream>
#include <thread>

/** @brief Run the requested prototype operation; no operation is implicit. */
int main(int argc, char **argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "probe") {
      display_device::WinDisplayDevice devices(std::make_shared<display_device::WinApiLayer>());
      std::cout << display_device::toJson(devices.enumAvailableDevices()) << '\n';
      return 0;
    }
    if (argc == 4 && std::string(argv[1]) == "provision") {
      std::cout << managed_vdd::windows_backend_t::provision(argv[2], argv[3]) << '\n';
      return 0;
    }
    if (argc < 3) {
      std::cerr << "probe | provision SIGNED_INF OWNER_FILE | inspect OWNER_FILE | recover OWNER_FILE | cycle OWNER_FILE [COUNT] | cycle-mode OWNER_FILE [COUNT]\n";
      return 2;
    }
    auto backend = std::make_unique<managed_vdd::windows_backend_t>(argv[2]);
    auto *observer = backend.get();
    if (std::string(argv[1]) == "inspect") {
      std::cout << observer->inspect() << '\n';
      return 0;
    }
    auto manager = std::make_shared<managed_vdd::manager_t>(std::move(backend));
    if (std::string(argv[1]) == "recover") {
      if (!manager->recover()) {
        throw std::runtime_error(manager->error());
      }
      std::cout << observer->inspect() << '\n';
      return 0;
    }
    const bool configure_mode = std::string(argv[1]) == "cycle-mode";
    if (std::string(argv[1]) != "cycle" && !configure_mode) {
      throw std::runtime_error("Unknown operation");
    }
    const int count = argc > 3 ? std::stoi(argv[3]) : 1;
    const int width = argc > 4 ? std::stoi(argv[4]) : 1920;
    const int height = argc > 5 ? std::stoi(argv[5]) : 1080;
    const int fps = argc > 6 ? std::stoi(argv[6]) : 60;
    if (count < 1 || count > 100) {
      throw std::runtime_error("Cycle count must be 1..100");
    }
    if (!manager->recover()) {
      throw std::runtime_error(manager->error());
    }
    for (int i = 0; i < count; ++i) {
      const auto begin = std::chrono::steady_clock::now();
      auto lease = manager->acquire(i);
      if (!lease || !lease->start()) {
        throw std::runtime_error("Activation failed: " + manager->error());
      }
      std::cout << "cycle=" << i + 1 << " display=" << observer->device_id() << " activation_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count() << std::endl;
      if (configure_mode) {
        try {
          if (!observer->configure(width, height, fps, false)) {
            throw std::runtime_error("Could not configure requested VDD SDR mode");
          }
          std::cout << "configured=" << width << 'x' << height << '@' << fps << " primary=true\n" << std::flush;
        } catch (...) {
          lease->finish();
          throw;
        }
      }
      std::this_thread::sleep_for(std::chrono::seconds(1));
      lease->finish();
      if (manager->state() != managed_vdd::state_e::idle) {
        throw std::runtime_error(manager->error());
      }
      std::cout << observer->inspect() << std::endl;
    }
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
