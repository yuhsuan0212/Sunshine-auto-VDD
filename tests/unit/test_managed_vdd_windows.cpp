/** @file tests/unit/test_managed_vdd_windows.cpp
 * @brief Read-only rejection tests; these never install or toggle a real device.
 */
#ifdef _WIN32
  #include "src/platform/windows/managed_vdd.h"

  #include <chrono>
  #include <fstream>
  #include <gtest/gtest.h>

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
}  // namespace
#endif
