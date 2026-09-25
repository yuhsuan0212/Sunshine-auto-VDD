/**
 * @file tests/unit/test_vdd_modes.cpp
 * @brief Verify mode insertion, idempotency and preservation of user VDD settings.
 */
#include "src/vdd_modes.h"

#include <gtest/gtest.h>

namespace {
  constexpr auto settings = R"(<?xml version='1.0' encoding='utf-8'?>
<vdd_settings>
  <gpu><friendlyname>default</friendlyname></gpu>
  <global><g_refresh_rate>60</g_refresh_rate><g_refresh_rate>120</g_refresh_rate></global>
  <resolutions>
    <!-- preserve this custom setting -->
    <resolution><width>1920</width><height>1080</height><refresh_rate>30</refresh_rate></resolution>
  </resolutions>
</vdd_settings>)";

  TEST(VddModes, ExistingGlobalModeDoesNotRewriteUserXml) {
    const auto result = managed_vdd::ensure_mode(settings, {1920, 1080, 60});
    ASSERT_TRUE(result);
    EXPECT_EQ(*result, settings);
  }

  TEST(VddModes, CustomResolutionUsesAdvertisedGlobalRefresh) {
    const auto result = managed_vdd::ensure_mode(settings, {1648, 839, 120});
    ASSERT_TRUE(result);
    EXPECT_NE(result->find("<width>1648</width><height>839</height>"), std::string::npos);
    EXPECT_NE(result->find("<!-- preserve this custom setting -->"), std::string::npos);
    EXPECT_EQ(result->find("<refresh_rate>120</refresh_rate>"), std::string::npos);
    EXPECT_EQ(managed_vdd::ensure_mode(*result, {1648, 839, 120}), result);
  }

  TEST(VddModes, CustomRefreshAddsOnlyRequestedMode) {
    const auto result = managed_vdd::ensure_mode(settings, {1920, 1080, 75});
    ASSERT_TRUE(result);
    EXPECT_NE(result->find("<width>1920</width><height>1080</height><refresh_rate>75</refresh_rate>"), std::string::npos);
    EXPECT_EQ(managed_vdd::ensure_mode(*result, {1920, 1080, 75}), result);
  }

  TEST(VddModes, NewResolutionAndRefreshArePaired) {
    const auto result = managed_vdd::ensure_mode(settings, {2304, 1440, 75});
    ASSERT_TRUE(result);
    EXPECT_NE(result->find("<width>2304</width><height>1440</height><refresh_rate>75</refresh_rate>"), std::string::npos);
  }

  TEST(VddModes, CommentedOutResolutionIsIgnored) {
    const std::string commented = std::string(settings) + "\n<!-- <resolution><width>1648</width><height>839</height><refresh_rate>75</refresh_rate></resolution> -->";
    const auto result = managed_vdd::ensure_mode(commented, {1648, 839, 75});
    ASSERT_TRUE(result);
    EXPECT_NE(result->find("<width>1648</width><height>839</height><refresh_rate>75</refresh_rate></resolution>\n    </resolutions>"), std::string::npos);
  }

  TEST(VddModes, UnsupportedOrMalformedInputsDoNotChangeXml) {
    EXPECT_FALSE(managed_vdd::ensure_mode(settings, {0, 720, 60}));
    EXPECT_FALSE(managed_vdd::ensure_mode(settings, {1280, 720, 241}));
    EXPECT_FALSE(managed_vdd::ensure_mode(settings, {1280, 720, 0}));
    EXPECT_FALSE(managed_vdd::ensure_mode("<vdd_settings><resolutions>", {1280, 720, 60}));
    EXPECT_FALSE(managed_vdd::ensure_mode(std::string(settings) + "<!-- open", {1280, 720, 60}));
  }
}  // namespace
