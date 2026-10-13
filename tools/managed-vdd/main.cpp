/**
 * @file tools/managed-vdd/main.cpp
 * @brief Provision, inspect, activate and deactivate an owned virtual display.
 */
#include "src/platform/windows/managed_vdd.h"

#include <cstdlib>
#include <ctime>
#include <display_device/json.h>
#include <display_device/logging.h>
#include <display_device/windows/win_api_layer.h>
#include <display_device/windows/win_display_device.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <type_traits>

namespace {
  /** @brief Appends timestamped lines to managed-vdd.log next to the owner file. */
  class run_log_t {
  public:
    /**
     * @brief Open the log, starting a new file once it exceeds 1 MiB.
     * @param owner_file Ownership file whose directory holds the log.
     */
    explicit run_log_t(const std::filesystem::path &owner_file) {
      const auto path = owner_file.parent_path() / "managed-vdd.log";
      std::error_code ec;
      if (std::filesystem::file_size(path, ec) > 1024 * 1024) {
        const auto old = std::filesystem::path(path).concat(".1");
        std::filesystem::remove(old, ec);
        std::filesystem::rename(path, old, ec);
      }
      out_.open(path, std::ios::app);
    }

    /**
     * @brief Write one line to the log and to stderr.
     * @param line Text to record.
     */
    void write(const std::string &line) {
      const auto now = std::time(nullptr);
      std::tm local {};
      localtime_s(&local, &now);
      std::ostringstream stamped;
      stamped << std::put_time(&local, "%F %T") << ' ' << line;
      out_ << stamped.str() << std::endl;
      std::cerr << stamped.str() << '\n';
    }

  private:
    std::ofstream out_;  ///< Append-only log file.
  };

  /** @brief Forwards to another backend and logs the outcome of every step. */
  class logged_backend_t: public managed_vdd::backend_t {
  public:
    /**
     * @brief Wrap a backend.
     * @param inner Backend performing the operations.
     * @param log Destination for step results.
     */
    logged_backend_t(managed_vdd::backend_t &inner, run_log_t &log):
        inner_(inner),
        log_(log) {}

    bool has_checkpoint() override {
      return step("has_checkpoint", [&] {
        return inner_.has_checkpoint();
      });
    }

    bool checkpoint() override {
      return step("checkpoint", [&] {
        return inner_.checkpoint();
      });
    }

    bool prepare_mode(managed_vdd::mode_t mode) override {
      return step("prepare_mode", [&] {
        return inner_.prepare_mode(mode);
      });
    }

    managed_vdd::result_e activate() override {
      return step("activate", [&] {
        return inner_.activate();
      });
    }

    bool wait_ready() override {
      return step("wait_ready", [&] {
        return inner_.wait_ready();
      });
    }

    bool restore() override {
      return step("restore", [&] {
        return inner_.restore();
      });
    }

    managed_vdd::result_e deactivate() override {
      return step("deactivate", [&] {
        return inner_.deactivate();
      });
    }

    bool wait_inactive() override {
      return step("wait_inactive", [&] {
        return inner_.wait_inactive();
      });
    }

    bool clear_checkpoint() override {
      return step("clear_checkpoint", [&] {
        return inner_.clear_checkpoint();
      });
    }

  private:
    /** @brief Describe a step result. */
    static std::string describe(bool value) {
      return value ? "true" : "false";
    }

    /** @brief Describe a PnP result. */
    static std::string describe(managed_vdd::result_e value) {
      using enum managed_vdd::result_e;
      return value == ok ? "ok" : value == failed ? "failed" :
                                                    "reboot_required";
    }

    /** @brief Run one step, logging its result or exception. */
    template<class F>
    std::invoke_result_t<F> step(const char *name, F &&operation) {
      try {
        auto result = operation();
        log_.write(std::string {"  "} + name + " -> " + describe(result));
        return result;
      } catch (const std::exception &e) {
        log_.write(std::string {"  "} + name + " threw: " + e.what());
        throw;
      }
    }

    managed_vdd::backend_t &inner_;  ///< Backend performing the operations.
    run_log_t &log_;  ///< Destination for step results.
  };
}  // namespace

/** @brief Run the requested operation; no operation is implicit. */
int main(int argc, char **argv) {
  // Sunshine discards the output of pre-display commands, so activate/deactivate keep their own log.
  std::optional<run_log_t> log;
  try {
    if (argc >= 3 && (std::string(argv[1]) == "activate" || std::string(argv[1]) == "deactivate" || std::string(argv[1]) == "recover")) {
      log.emplace(argv[2]);
      display_device::Logger::get().setLogLevel(display_device::Logger::LogLevel::info);
      display_device::Logger::get().setCustomCallback([&log](display_device::Logger::LogLevel, std::string value) {
        log->write("  [libdisplaydevice] " + value);
      });
      std::string command;
      for (int i = 1; i < argc; ++i) {
        command += std::string(i > 1 ? " " : "") + argv[i];
      }
      log->write("run: " + command);
    }
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
      std::cerr << "probe | provision SIGNED_INF OWNER_FILE | inspect OWNER_FILE | activate OWNER_FILE [WIDTH HEIGHT FPS] | deactivate OWNER_FILE\n";
      return 2;
    }
    auto backend = std::make_unique<managed_vdd::windows_backend_t>(argv[2]);
    auto *observer = backend.get();
    if (std::string(argv[1]) == "recover") {
      if (argc != 5) {
        throw std::runtime_error("recover OWNER_FILE SERVICE_NAME ABSOLUTE_DISPLAY_STATE_FILE");
      }
      const auto result = observer->recover(std::filesystem::path(argv[3]).wstring(), argv[4]);
      using enum managed_vdd::recovery_result_e;
      const char *description = result == idle ? "idle" : result == busy   ? "deferred: owner is live, unknown, or not on console" :
                                                        result == restored ? "original desktop restored" :
                                                        result == rescued  ? "physical desktop rescued; baseline archived" :
                                                                             "failed; recovery will be retried";
      log->write(std::string {"recovery: "} + description);
      return result == failed ? 1 : 0;
    }
    if (std::string(argv[1]) == "inspect") {
      std::cout << observer->inspect() << '\n';
      return 0;
    }
    if (std::string(argv[1]) == "activate") {
      observer->require_matching_host();
      // Mode defaults to the Sunshine pre-display command environment.
      const auto env_int = [](const char *name, int fallback) {
        const char *value = std::getenv(name);
        return value && *value ? std::stoi(value) : fallback;
      };
      const managed_vdd::mode_t mode {
        argc > 3 ? std::stoi(argv[3]) : env_int("SUNSHINE_CLIENT_WIDTH", 1920),
        argc > 4 ? std::stoi(argv[4]) : env_int("SUNSHINE_CLIENT_HEIGHT", 1080),
        argc > 5 ? std::stoi(argv[5]) : env_int("SUNSHINE_CLIENT_FPS", 60),
      };
      log->write("mode: " + std::to_string(mode.width) + 'x' + std::to_string(mode.height) + '@' + std::to_string(mode.fps));
      logged_backend_t logged(*backend, *log);
      if (!managed_vdd::activate_for_stream(logged, mode)) {
        throw std::runtime_error("Could not activate VDD; see managed-vdd.log");
      }
      log->write("activated display=" + observer->device_id());
      std::cout << "activated=" << mode.width << 'x' << mode.height << '@' << mode.fps << " display=" << observer->device_id() << '\n';
      return 0;
    }
    if (std::string(argv[1]) == "deactivate") {
      observer->require_matching_host();
      logged_backend_t logged(*backend, *log);
      if (!managed_vdd::deactivate_after_stream(logged)) {
        throw std::runtime_error("Could not deactivate VDD; the device and its checkpoint were kept for recovery");
      }
      log->write("deactivated");
      std::cout << "deactivated\n";
      return 0;
    }
    throw std::runtime_error("Unknown operation");
  } catch (const std::exception &e) {
    if (log) {
      log->write(std::string {"failed: "} + e.what());
    } else {
      std::cerr << e.what() << '\n';
    }
    return 1;
  }
}
