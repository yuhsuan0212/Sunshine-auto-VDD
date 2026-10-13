/**
 * @file tools/managed-vdd/tests/test_recovery.cpp
 * @brief Fault injection for abandoned-process recovery without changing the desktop.
 */
#include "src/managed_vdd_recovery.h"

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
  using namespace managed_vdd;

  /** @brief Combined fake with explicit display-writer and persistence assertions. */
  class RecoveryTest: public testing::Test, public backend_t, public recovery_host_t {
  public:
    bool saved = true;  ///< Unfinished transaction on disk.
    owner_state_e owner = owner_state_e::abandoned;  ///< Recorded process state.
    bool quiet = false;  ///< Service and its child have stopped.
    bool stop_ok = true;  ///< Quiesce result.
    bool restore_ok = true;  ///< Exact baseline result.
    bool rescue_ok = false;  ///< Physical-output fallback result.
    bool inactive_ok = true;  ///< Verified device shutdown.
    bool desktop_ok = true;  ///< Desktop survived device shutdown.
    bool retire_ok = true;  ///< Obsolete Sunshine persistence retirement.
    bool resume_ok = true;  ///< Service restart result.
    bool clear_ok = true;  ///< Checkpoint deletion result.
    bool throw_restore = false;  ///< Simulate interrupted display API calls.
    bool throw_retire = false;  ///< Simulate an inaccessible host state file.
    result_e disable_result = result_e::ok;  ///< PnP operation outcome.
    std::vector<std::string> trace;  ///< Observable operation order.

    bool has_checkpoint() override {
      return saved;
    }

    bool checkpoint() override {
      ADD_FAILURE();
      return false;
    }

    bool prepare_mode(managed_vdd::mode_t) override {
      ADD_FAILURE();
      return false;
    }

    result_e activate() override {
      ADD_FAILURE();
      return result_e::failed;
    }

    bool wait_ready() override {
      ADD_FAILURE();
      return false;
    }

    owner_state_e owner_state() override {
      return owner;
    }

    bool quiesce() override {
      trace.emplace_back("stop");
      quiet = stop_ok;
      return stop_ok;
    }

    bool restore() override {
      EXPECT_TRUE(quiet);
      trace.emplace_back("restore");
      if (throw_restore) {
        throw std::runtime_error("partial restore");
      }
      return restore_ok;
    }

    bool rescue_physical() override {
      EXPECT_TRUE(quiet);
      trace.emplace_back("rescue");
      return rescue_ok;
    }

    result_e deactivate() override {
      EXPECT_TRUE(quiet);
      trace.emplace_back("disable");
      return disable_result;
    }

    bool wait_inactive() override {
      trace.emplace_back("verify");
      return inactive_ok;
    }

    bool verify_desktop(bool) override {
      EXPECT_TRUE(quiet);
      trace.emplace_back("desktop");
      return desktop_ok;
    }

    bool retire_state(bool rescued) override {
      EXPECT_TRUE(quiet);
      trace.emplace_back(rescued ? "archive-rescue" : "archive-exact");
      if (throw_retire) {
        throw std::runtime_error("state locked");
      }
      return retire_ok;
    }

    bool clear_checkpoint() override {
      trace.emplace_back("clear");
      if (clear_ok) {
        saved = false;
      }
      return clear_ok;
    }

    bool resume() override {
      trace.emplace_back("resume");
      quiet = false;
      return resume_ok;
    }

    recovery_result_e run() {
      return recover_abandoned(*this, *this);
    }
  };

  TEST_F(RecoveryTest, NoCheckpointNeverStopsTheService) {
    saved = false;
    EXPECT_EQ(run(), recovery_result_e::idle);
    EXPECT_TRUE(trace.empty());
  }

  TEST_F(RecoveryTest, LiveOrUnknownOwnerIsNeverRecovered) {
    for (const auto state : {owner_state_e::alive, owner_state_e::unknown}) {
      owner = state;
      EXPECT_EQ(run(), recovery_result_e::busy);
      EXPECT_TRUE(saved);
      EXPECT_TRUE(trace.empty());
    }
  }

  TEST_F(RecoveryTest, RebootRecoveryRetiresHostStateOnlyAfterVerifiedInactiveDevice) {
    EXPECT_EQ(run(), recovery_result_e::restored);
    EXPECT_EQ(trace, (std::vector<std::string> {"stop", "restore", "disable", "verify", "desktop", "archive-exact", "clear", "resume"}));
    EXPECT_FALSE(saved);
    trace.clear();
    EXPECT_EQ(run(), recovery_result_e::idle);
    EXPECT_TRUE(trace.empty());
  }

  TEST_F(RecoveryTest, HostThatCannotStopPreventsAllDisplayWrites) {
    stop_ok = false;
    EXPECT_EQ(run(), recovery_result_e::failed);
    EXPECT_EQ(trace, (std::vector<std::string> {"stop", "resume"}));
    EXPECT_TRUE(saved);
  }

  TEST_F(RecoveryTest, MissingOldMonitorCanUseVerifiedPhysicalRescue) {
    restore_ok = false;
    rescue_ok = true;
    EXPECT_EQ(run(), recovery_result_e::rescued);
    EXPECT_EQ(trace, (std::vector<std::string> {"stop", "restore", "rescue", "disable", "verify", "desktop", "archive-rescue", "clear", "resume"}));
  }

  TEST_F(RecoveryTest, PartialRestoreExceptionStillAllowsRescue) {
    throw_restore = true;
    rescue_ok = true;
    EXPECT_EQ(run(), recovery_result_e::rescued);
  }

  TEST_F(RecoveryTest, NoPhysicalRescueKeepsDeviceAndBothJournals) {
    restore_ok = false;
    EXPECT_EQ(run(), recovery_result_e::failed);
    EXPECT_EQ(trace, (std::vector<std::string> {"stop", "restore", "rescue", "resume"}));
    EXPECT_TRUE(saved);
  }

  TEST_F(RecoveryTest, DeviceRequiringRebootKeepsRecoveryState) {
    disable_result = result_e::reboot_required;
    EXPECT_EQ(run(), recovery_result_e::failed);
    EXPECT_EQ(trace, (std::vector<std::string> {"stop", "restore", "disable", "resume"}));
    EXPECT_TRUE(saved);
  }

  TEST_F(RecoveryTest, FailedVerificationNeverRetiresHostState) {
    inactive_ok = false;
    EXPECT_EQ(run(), recovery_result_e::failed);
    EXPECT_EQ(trace.back(), "resume");
    EXPECT_EQ(trace.size(), 5);
    EXPECT_TRUE(saved);
  }

  TEST_F(RecoveryTest, ArchiveFailureKeepsCheckpointAndResumesHost) {
    for (bool throws : {false, true}) {
      trace.clear();
      retire_ok = false;
      throw_retire = throws;
      EXPECT_EQ(run(), recovery_result_e::failed);
      EXPECT_TRUE(saved);
      EXPECT_EQ(trace.back(), "resume");
    }
  }

  TEST_F(RecoveryTest, DesktopLostDuringDeviceRemovalKeepsBothRecoveryRecords) {
    desktop_ok = false;
    EXPECT_EQ(run(), recovery_result_e::failed);
    EXPECT_TRUE(saved);
    EXPECT_EQ(trace, (std::vector<std::string> {"stop", "restore", "disable", "verify", "desktop", "resume"}));
  }

  TEST_F(RecoveryTest, JournalDeletionFailureCanBeRetried) {
    clear_ok = false;
    EXPECT_EQ(run(), recovery_result_e::failed);
    EXPECT_TRUE(saved);
    clear_ok = true;
    EXPECT_EQ(run(), recovery_result_e::restored);
  }

  TEST_F(RecoveryTest, RestartFailureIsReportedEvenAfterDisplayRestoration) {
    resume_ok = false;
    EXPECT_EQ(run(), recovery_result_e::failed);
    EXPECT_FALSE(saved);
  }
}  // namespace
