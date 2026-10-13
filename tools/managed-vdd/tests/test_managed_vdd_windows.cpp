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
  #include <windows.h>

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

  TEST(ManagedVddProcessOwner, MissingIdentityIsUnknown) {
    EXPECT_EQ(managed_vdd::query_process_owner(0, 1), managed_vdd::owner_state_e::unknown);
    EXPECT_EQ(managed_vdd::query_process_owner(GetCurrentProcessId(), 0), managed_vdd::owner_state_e::unknown);
  }

  TEST(ManagedVddProcessOwner, LiveProcessAndReusedPidAreDistinguished) {
    FILETIME creation {}, exit {}, kernel {}, user {};
    ASSERT_TRUE(GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user));
    const uint64_t stamp = (static_cast<uint64_t>(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
    EXPECT_EQ(managed_vdd::query_process_owner(GetCurrentProcessId(), stamp), managed_vdd::owner_state_e::alive);
    EXPECT_EQ(managed_vdd::query_process_owner(GetCurrentProcessId(), stamp + 1), managed_vdd::owner_state_e::abandoned);
  }

  TEST(ManagedVddProcessOwner, ExitedProcessIsAbandonedEvenWhileHandleExists) {
    STARTUPINFOW startup {sizeof(startup)};
    PROCESS_INFORMATION child {};
    wchar_t system[MAX_PATH] {};
    ASSERT_NE(GetSystemDirectoryW(system, MAX_PATH), 0);
    const auto executable = std::filesystem::path(system) / L"cmd.exe";
    wchar_t command[] = L"cmd.exe /d /c exit 0";
    ASSERT_TRUE(CreateProcessW(executable.c_str(), command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child));
    FILETIME creation {}, exit {}, kernel {}, user {};
    EXPECT_TRUE(GetProcessTimes(child.hProcess, &creation, &exit, &kernel, &user));
    const uint64_t stamp = (static_cast<uint64_t>(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
    EXPECT_EQ(WaitForSingleObject(child.hProcess, 10000), WAIT_OBJECT_0);
    EXPECT_EQ(managed_vdd::query_process_owner(child.dwProcessId, stamp), managed_vdd::owner_state_e::abandoned);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
  }

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
