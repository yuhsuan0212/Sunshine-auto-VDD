/**
 * @file tools/managed-vdd/tests/test_managed_vdd_windows.cpp
 * @brief Ownership rejection and baseline decisions; these never install or toggle a real device.
 */
#ifdef _WIN32
  #include "src/platform/windows/managed_vdd.h"

  #include <chrono>
  #include <fstream>
  #include <gtest/gtest.h>
  #include <vector>

namespace {
  /** @brief Temporary ownership input for rejection tests. */
  class ManagedVddWindowsTest: public testing::Test {
  protected:
    std::filesystem::path file = std::filesystem::temp_directory_path() / ("vdd-invalid-owner-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");  ///< Isolated test input.

    /** @brief Remove only this test's file. */
    void TearDown() override {
      std::filesystem::remove(file);
    }

    /** @brief Write an ownership document. */
    void write(const std::string &text) {
      std::ofstream out(file);
      out << text;
    }
  };

  TEST_F(ManagedVddWindowsTest, MissingOwnershipIsRejected) {
    EXPECT_THROW(managed_vdd::windows_backend_t backend(file), std::exception);
  }

  TEST_F(ManagedVddWindowsTest, CorruptOwnershipIsRejected) {
    write("{bad json");
    EXPECT_THROW(managed_vdd::windows_backend_t backend(file), std::exception);
  }

  TEST_F(ManagedVddWindowsTest, UnknownSchemaIsRejected) {
    write(R"({"version":2})");
    EXPECT_THROW(managed_vdd::windows_backend_t backend(file), std::exception);
  }

  TEST_F(ManagedVddWindowsTest, EmptyTokenIsRejected) {
    write(R"({"version":1,"instance_id":"ROOT\\NONEXISTENT_VDD_TEST\\0000","owner_token":""})");
    EXPECT_THROW(managed_vdd::windows_backend_t backend(file), std::exception);
  }

  TEST_F(ManagedVddWindowsTest, MissingExactInstanceIsRejected) {
    write(R"({"version":1,"instance_id":"ROOT\\NONEXISTENT_VDD_TEST\\0000","owner_token":"vdd-test-token"})");
    EXPECT_THROW(managed_vdd::windows_backend_t backend(file), std::exception);
  }

  using managed_vdd::active_target_t;
  using managed_vdd::target_e;
  using targets_t = std::vector<active_target_t>;
  using ids_t = display_device::StringSet;

  /** @brief An identified monitor. */
  active_target_t monitor(const std::string &id) {
    return {target_e::monitor, id};
  }

  const active_target_t no_monitor {target_e::no_monitor, {}};  ///< A path left behind by an unplugged monitor.
  const active_target_t unknown {target_e::unknown, {}};  ///< A path that could not be queried or identified.

  TEST(ManagedVddHeadless, FailedQueryIsHeadlessOnlyWhenWindowsReportsZeroPaths) {
    EXPECT_TRUE(managed_vdd::is_headless(nullptr, true));
    EXPECT_FALSE(managed_vdd::is_headless(nullptr, false));
  }

  TEST(ManagedVddHeadless, NoActivePathIsHeadless) {
    const targets_t targets;
    EXPECT_TRUE(managed_vdd::is_headless(&targets, false));
  }

  TEST(ManagedVddHeadless, PathsLeftByDisconnectedMonitorsAreHeadless) {
    const targets_t targets {no_monitor, no_monitor};
    EXPECT_TRUE(managed_vdd::is_headless(&targets, false));
  }

  TEST(ManagedVddHeadless, AnyMonitorIsNotHeadless) {
    // Even if the topology cannot be identified afterwards, this must not become an empty baseline.
    const targets_t targets {no_monitor, monitor("A")};
    EXPECT_FALSE(managed_vdd::is_headless(&targets, false));
    EXPECT_FALSE(managed_vdd::is_headless(&targets, true));
  }

  TEST(ManagedVddHeadless, UnqueryableTargetIsNotHeadless) {
    const targets_t targets {no_monitor, unknown};
    EXPECT_FALSE(managed_vdd::is_headless(&targets, false));
  }

  TEST(ManagedVddBaseline, SameDisplaysAreComplete) {
    const targets_t targets {monitor("A"), monitor("B"), no_monitor};
    EXPECT_TRUE(managed_vdd::is_complete_baseline(targets, ids_t {"A", "B"}));
  }

  TEST(ManagedVddBaseline, MonitorMissingFromTopologyIsIncomplete) {
    // One of two monitors could not be identified (see libdisplaydevice's TransientDisplayIssues test).
    const targets_t targets {monitor("A"), monitor("B")};
    EXPECT_FALSE(managed_vdd::is_complete_baseline(targets, ids_t {"A"}));
    EXPECT_FALSE(managed_vdd::is_complete_baseline(targets, ids_t {}));
  }

  TEST(ManagedVddBaseline, SameCountWithDifferentDisplaysIsIncomplete) {
    // B dropped out of the later topology query while a hot-plugged C appeared.
    const targets_t targets {monitor("A"), monitor("B")};
    EXPECT_FALSE(managed_vdd::is_complete_baseline(targets, ids_t {"A", "C"}));
  }

  TEST(ManagedVddBaseline, UnidentifiedTargetIsIncomplete) {
    const targets_t targets {monitor("A"), unknown};
    EXPECT_FALSE(managed_vdd::is_complete_baseline(targets, ids_t {"A"}));
    EXPECT_FALSE(managed_vdd::is_complete_baseline(targets, ids_t {"A", "B"}));
  }
}  // namespace
#endif
