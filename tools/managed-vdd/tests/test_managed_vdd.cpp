/**
 * @file tools/managed-vdd/tests/test_managed_vdd.cpp
 * @brief Fault injection for the VDD activation sequences without touching real displays.
 */
#include "src/managed_vdd.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
  using namespace managed_vdd;

  /** @brief Fake platform with observable ordering and injectable failures. */
  struct fake_backend_t: backend_t {
    std::vector<std::string> calls;  ///< Completed operation trace.
    bool saved = false;  ///< Durable checkpoint exists.
    bool checkpoint_ok = true;  ///< Snapshot persistence result.
    bool prepare_ok = true;  ///< Mode preparation result.
    bool ready = true;  ///< Display enumeration result.
    bool restore_ok = true;  ///< Original physical/headless baseline restored.
    bool inactive = true;  ///< Verified inactive state.
    bool clear_ok = true;  ///< Journal deletion result.
    bool throws = false;  ///< Simulated Windows API exception.
    result_e activation = result_e::ok;  ///< PnP activation result.
    result_e deactivation = result_e::ok;  ///< PnP deactivation result.

    bool has_checkpoint() override {
      return saved;
    }

    bool checkpoint() override {
      calls.push_back("checkpoint");
      if (saved) {
        return false;  // Like the Windows backend, never overwrite an existing checkpoint.
      }
      saved = checkpoint_ok;
      return checkpoint_ok;
    }

    bool prepare_mode(managed_vdd::mode_t) override {
      calls.push_back("prepare_mode");
      return prepare_ok;
    }

    result_e activate() override {
      calls.push_back("activate");
      if (throws) {
        throw std::runtime_error("API failure");
      }
      return activation;
    }

    bool wait_ready() override {
      calls.push_back("ready");
      return ready;
    }

    bool restore() override {
      calls.push_back("restore");
      return restore_ok;
    }

    result_e deactivate() override {
      calls.push_back("deactivate");
      return deactivation;
    }

    bool wait_inactive() override {
      calls.push_back("inactive");
      return inactive;
    }

    bool clear_checkpoint() override {
      calls.push_back("clear");
      if (clear_ok) {
        saved = false;
      }
      return clear_ok;
    }
  };

  using calls_t = std::vector<std::string>;

  TEST(ManagedVddStream, DoActivatesAndUndoRestoresBeforeDisabling) {
    fake_backend_t backend;
    EXPECT_TRUE(activate_for_stream(backend, {1920, 1080, 60}));
    EXPECT_TRUE(deactivate_after_stream(backend));
    EXPECT_EQ(backend.calls, (calls_t {"checkpoint", "prepare_mode", "activate", "ready", "restore", "deactivate", "inactive", "clear"}));
    EXPECT_FALSE(backend.saved);
  }

  TEST(ManagedVddStream, DoCleansUpAnActivationWhoseUndoNeverRan) {
    fake_backend_t backend;
    backend.saved = true;
    EXPECT_TRUE(activate_for_stream(backend, {1920, 1080, 60}));
    EXPECT_EQ(backend.calls, (calls_t {"checkpoint", "restore", "deactivate", "inactive", "clear", "checkpoint", "prepare_mode", "activate", "ready"}));
  }

  TEST(ManagedVddStream, FailedRestoreKeepsDeviceAndCheckpointThroughDoAndUndo) {
    fake_backend_t backend;
    backend.saved = true;
    backend.restore_ok = false;
    // Sunshine runs the matching Undo after a failed Do.
    EXPECT_FALSE(activate_for_stream(backend, {1920, 1080, 60}));
    EXPECT_FALSE(deactivate_after_stream(backend));
    EXPECT_EQ(backend.calls, (calls_t {"checkpoint", "restore", "restore"}));
    EXPECT_TRUE(backend.saved);
  }

  TEST(ManagedVddStream, FailedActivationRestoresAndDisables) {
    fake_backend_t backend;
    backend.activation = result_e::failed;
    EXPECT_FALSE(activate_for_stream(backend, {1920, 1080, 60}));
    EXPECT_EQ(backend.calls, (calls_t {"checkpoint", "prepare_mode", "activate", "restore", "deactivate", "inactive", "clear"}));
    EXPECT_FALSE(backend.saved);
  }

  TEST(ManagedVddStream, UndoWithoutCheckpointOnlyDisables) {
    fake_backend_t backend;
    EXPECT_TRUE(deactivate_after_stream(backend));
    EXPECT_TRUE(deactivate_after_stream(backend));
    EXPECT_EQ(backend.calls, (calls_t {"deactivate", "inactive", "clear", "deactivate", "inactive", "clear"}));
  }

  TEST(ManagedVddStream, UndoFailureKeepsCheckpoint) {
    fake_backend_t backend;
    backend.saved = true;
    backend.deactivation = result_e::reboot_required;
    EXPECT_FALSE(deactivate_after_stream(backend));
    EXPECT_TRUE(backend.saved);
  }

  TEST(ManagedVddStream, CheckpointFailureNeverActivates) {
    fake_backend_t backend;
    backend.checkpoint_ok = false;
    EXPECT_FALSE(activate_for_stream(backend, {1920, 1080, 60}));
    EXPECT_EQ(std::ranges::count(backend.calls, "activate"), 0);
  }

  TEST(ManagedVddStream, UnsupportedModeRollsBackWithoutActivating) {
    fake_backend_t backend;
    backend.prepare_ok = false;
    EXPECT_FALSE(activate_for_stream(backend, {1920, 1080, 60}));
    EXPECT_EQ(backend.calls, (calls_t {"checkpoint", "prepare_mode", "restore", "deactivate", "inactive", "clear"}));
    EXPECT_FALSE(backend.saved);
  }

  TEST(ManagedVddStream, RebootRequiredActivationRollsBack) {
    fake_backend_t backend;
    backend.activation = result_e::reboot_required;
    EXPECT_FALSE(activate_for_stream(backend, {1920, 1080, 60}));
    EXPECT_FALSE(backend.saved);
  }

  TEST(ManagedVddStream, ExceptionKeepsCheckpointForUndo) {
    fake_backend_t backend;
    backend.throws = true;
    // The command fails; Sunshine then runs Undo, which still finds the checkpoint.
    EXPECT_THROW(activate_for_stream(backend, {1920, 1080, 60}), std::runtime_error);
    EXPECT_TRUE(backend.saved);
    EXPECT_TRUE(deactivate_after_stream(backend));
    EXPECT_FALSE(backend.saved);
  }

  TEST(ManagedVddStream, LingeringTargetKeepsCheckpoint) {
    fake_backend_t backend;
    backend.saved = true;
    backend.inactive = false;
    EXPECT_FALSE(deactivate_after_stream(backend));
    EXPECT_TRUE(backend.saved);
  }

  TEST(ManagedVddStream, UndoCanBeRepeatedAfterClearFailure) {
    fake_backend_t backend;
    backend.saved = true;
    backend.clear_ok = false;
    EXPECT_FALSE(deactivate_after_stream(backend));
    EXPECT_TRUE(backend.saved);
    backend.clear_ok = true;
    EXPECT_TRUE(deactivate_after_stream(backend));
    EXPECT_FALSE(backend.saved);
  }
}  // namespace
