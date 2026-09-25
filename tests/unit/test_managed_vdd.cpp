/** @file tests/unit/test_managed_vdd.cpp
 * @brief Fault injection for managed VDD transactions without touching real displays.
 */
#include "src/managed_vdd.h"

#include <gtest/gtest.h>
#include <thread>
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

  /** @brief Fixture exposing a fake backend while the manager owns its lifetime. */
  class ManagedVddTest: public testing::Test {
  protected:
    fake_backend_t *backend = new fake_backend_t;  ///< Non-owning observer.
    std::shared_ptr<manager_t> manager = std::make_shared<manager_t>(std::unique_ptr<backend_t>(backend));  ///< Manager under test.
  };

  TEST_F(ManagedVddTest, LastCaptureFinishesBeforeCleanup) {
    auto first = manager->acquire(1);
    ASSERT_TRUE(first);
    ASSERT_TRUE(first->start());
    auto second = manager->acquire(2);
    ASSERT_TRUE(second);
    ASSERT_TRUE(second->start());
    first->finish();
    EXPECT_EQ(manager->state(), state_e::streaming);
    EXPECT_EQ(backend->calls.size(), 3u);
    second->finish();
    EXPECT_EQ(backend->calls, (std::vector<std::string> {"checkpoint", "activate", "ready", "restore", "deactivate", "inactive", "clear"}));
    EXPECT_EQ(manager->state(), state_e::idle);
    second->finish();
    EXPECT_EQ(backend->calls.size(), 7u);
  }

  TEST_F(ManagedVddTest, PendingTimeoutCannotStartOrLeakDisplay) {
    auto lease = manager->acquire(1);
    ASSERT_TRUE(lease);
    lease->expire();
    EXPECT_FALSE(lease->start());
    EXPECT_EQ(manager->state(), state_e::idle);
    EXPECT_FALSE(backend->saved);
  }

  TEST_F(ManagedVddTest, ModePreparedBeforeDriverActivation) {
    auto lease = manager->acquire(1, {1648, 839, 60});
    ASSERT_TRUE(lease);
    EXPECT_EQ(backend->calls, (std::vector<std::string> {"checkpoint", "prepare_mode", "activate", "ready"}));
    lease->expire();
  }

  TEST_F(ManagedVddTest, UnsupportedModeRollsBackWithoutActivating) {
    backend->prepare_ok = false;
    EXPECT_FALSE(manager->acquire(1, {1648, 839, 60}));
    EXPECT_EQ(backend->calls, (std::vector<std::string> {"checkpoint", "prepare_mode", "restore", "deactivate", "inactive", "clear"}));
    EXPECT_EQ(manager->state(), state_e::idle);
  }

  TEST_F(ManagedVddTest, OldLeaseCannotReleaseReusedLaunchId) {
    auto old = manager->acquire(1);
    old->expire();
    auto current = manager->acquire(1);
    old->finish();
    old.reset();
    EXPECT_EQ(manager->state(), state_e::pending);
    ASSERT_TRUE(current->start());
    current->finish();
  }

  TEST_F(ManagedVddTest, TimerCannotReleaseActiveCapture) {
    auto lease = manager->acquire(1);
    ASSERT_TRUE(lease->start());
    lease->expire();
    EXPECT_EQ(manager->state(), state_e::streaming);
    EXPECT_TRUE(backend->saved);
    lease->finish();
  }

  TEST_F(ManagedVddTest, PendingDestructorRollsBackLaunchFailure) {
    {
      auto lease = manager->acquire(1);
      ASSERT_TRUE(lease);
    }
    EXPECT_EQ(manager->state(), state_e::idle);
  }

  TEST_F(ManagedVddTest, RejectsConcurrentPendingHandshake) {
    auto lease = manager->acquire(1);
    EXPECT_FALSE(manager->acquire(2));
    EXPECT_FALSE(manager->acquire(1));
    EXPECT_EQ(backend->calls.size(), 3u);
  }

  TEST_F(ManagedVddTest, PendingReconnectKeepsDeviceAfterPreviousCaptureStops) {
    auto first = manager->acquire(1);
    ASSERT_TRUE(first->start());
    auto pending = manager->acquire(2);
    first->finish();
    EXPECT_EQ(manager->state(), state_e::pending);
    EXPECT_EQ(backend->calls.size(), 3u);
    pending->expire();
    EXPECT_EQ(manager->state(), state_e::idle);
  }

  TEST_F(ManagedVddTest, RestoreFailureRetainsDeviceAndCheckpoint) {
    auto lease = manager->acquire(1);
    backend->restore_ok = false;
    lease->expire();
    EXPECT_EQ(manager->state(), state_e::recovery_required);
    EXPECT_EQ(backend->calls.back(), "restore");
    EXPECT_TRUE(backend->saved);
    EXPECT_FALSE(manager->acquire(2));
    backend->restore_ok = true;
    EXPECT_TRUE(manager->recover());
    EXPECT_EQ(manager->state(), state_e::idle);
  }

  TEST_F(ManagedVddTest, MissingDisplayRollsBackBeforeIdle) {
    backend->ready = false;
    EXPECT_FALSE(manager->acquire(1));
    EXPECT_EQ(manager->state(), state_e::idle);
    EXPECT_EQ(backend->calls.back(), "clear");
  }

  TEST_F(ManagedVddTest, FailedActivationIsReconciled) {
    backend->activation = result_e::failed;
    EXPECT_FALSE(manager->acquire(1));
    EXPECT_EQ(manager->state(), state_e::idle);
  }

  TEST_F(ManagedVddTest, RebootRequiredIsNotIdle) {
    backend->activation = result_e::reboot_required;
    EXPECT_FALSE(manager->acquire(1));
    EXPECT_EQ(manager->state(), state_e::reboot_required);
    EXPECT_TRUE(backend->saved);
  }

  TEST_F(ManagedVddTest, CheckpointFailureNeverActivates) {
    backend->checkpoint_ok = false;
    EXPECT_FALSE(manager->acquire(1));
    EXPECT_EQ(backend->calls, (std::vector<std::string> {"checkpoint"}));
  }

  TEST_F(ManagedVddTest, FailedDeactivationIsNotIdle) {
    auto lease = manager->acquire(1);
    backend->deactivation = result_e::reboot_required;
    lease->expire();
    EXPECT_EQ(manager->state(), state_e::reboot_required);
    EXPECT_EQ(backend->calls.back(), "deactivate");
  }

  TEST_F(ManagedVddTest, LingeringTargetRetainsCheckpoint) {
    auto lease = manager->acquire(1);
    backend->inactive = false;
    lease->expire();
    EXPECT_EQ(manager->state(), state_e::recovery_required);
    EXPECT_TRUE(backend->saved);
  }

  TEST_F(ManagedVddTest, ClearFailureCanBeRecoveredIdempotently) {
    auto lease = manager->acquire(1);
    backend->clear_ok = false;
    lease->expire();
    EXPECT_EQ(manager->state(), state_e::recovery_required);
    backend->clear_ok = true;
    EXPECT_TRUE(manager->recover());
    EXPECT_TRUE(manager->recover());
  }

  TEST_F(ManagedVddTest, CrashCheckpointRecoveredWithoutNewActivation) {
    backend->saved = true;
    EXPECT_FALSE(manager->acquire(1));
    EXPECT_TRUE(manager->recover());
    EXPECT_EQ(backend->calls.front(), "restore");
  }

  TEST_F(ManagedVddTest, RecoveryCannotInterruptCapture) {
    auto lease = manager->acquire(1);
    ASSERT_TRUE(lease->start());
    EXPECT_FALSE(manager->recover());
    lease->finish();
  }

  TEST_F(ManagedVddTest, ExceptionRetainsCheckpointForRecovery) {
    backend->throws = true;
    EXPECT_FALSE(manager->acquire(1));
    EXPECT_TRUE(backend->saved);
    EXPECT_EQ(manager->error(), "API failure");
    backend->throws = false;
    EXPECT_TRUE(manager->recover());
  }

  TEST_F(ManagedVddTest, OneHundredReconnectsLeaveNoCheckpoint) {
    for (std::uint32_t id = 0; id < 100; ++id) {
      auto lease = manager->acquire(id);
      ASSERT_TRUE(lease);
      ASSERT_TRUE(lease->start());
      lease->finish();
      EXPECT_FALSE(backend->saved);
    }
    EXPECT_EQ(backend->calls.size(), 700u);
  }

  TEST_F(ManagedVddTest, TimerAndCaptureStartAreSerialized) {
    for (std::uint32_t id = 0; id < 100; ++id) {
      auto lease = manager->acquire(id);
      ASSERT_TRUE(lease);
      bool started = false;
      std::thread stream([&] {
        started = lease->start();
      });
      std::thread timer([&] {
        lease->expire();
      });
      stream.join();
      timer.join();
      EXPECT_EQ(manager->state(), started ? state_e::streaming : state_e::idle);
      lease->finish();
    }
  }

  TEST(ManagedVddConstruction, RejectsNullBackend) {
    EXPECT_THROW(manager_t(nullptr), std::invalid_argument);
  }

  TEST(ManagedVddTopology, ExclusiveStreamRemovesPhysicalAndOtherVirtualOutputs) {
    const topology_t before {{"physical"}, {"other-vdd"}};
    EXPECT_EQ(stream_topology(before, "owned-vdd", true), (topology_t {{"owned-vdd"}}));
    EXPECT_EQ(before, (topology_t {{"physical"}, {"other-vdd"}}));
  }

  TEST(ManagedVddTopology, ExtendedModeKeepsCurrentOutputsAndAvoidsDuplicates) {
    const topology_t before {{"physical"}};
    const topology_t extended {{"physical"}, {"owned-vdd"}};
    EXPECT_EQ(stream_topology(before, "owned-vdd", false), extended);
    EXPECT_EQ(stream_topology(extended, "owned-vdd", false), extended);
  }
}  // namespace
