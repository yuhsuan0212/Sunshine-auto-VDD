/**
 * @file src/vdd_modes.cpp
 * @brief Preserve existing VDD settings while adding a client-requested mode.
 */
#include "vdd_modes.h"

#include <regex>
#include <string_view>
#include <vector>

namespace managed_vdd {
  namespace {
    constexpr std::size_t max_settings_bytes = 512 * 1024;  ///< Bound XML scan and recovery journal size.

    /**
     * @brief Hide XML comments while retaining byte offsets in the original document.
     * @param xml Source XML.
     * @return Copy with comment bytes replaced by spaces, or no value for an open comment.
     */
    std::optional<std::string> without_comments(const std::string &xml) {
      auto searchable = xml;
      std::size_t start = 0;
      while ((start = searchable.find("<!--", start)) != std::string::npos) {
        const auto end = searchable.find("-->", start + 4);
        if (end == std::string::npos) {
          return std::nullopt;
        }
        searchable.replace(start, end + 3 - start, end + 3 - start, ' ');
        start = end + 3;
      }
      return searchable;
    }

    /**
     * @brief Read a decimal child element from a resolution block.
     * @param block A resolution element without comments.
     * @param tag Child name.
     * @return Parsed integer or no value when absent or malformed.
     */
    std::optional<int> decimal_child(const std::string &block, std::string_view tag) {
      const std::regex pattern("<" + std::string(tag) + R"(\s*>\s*([0-9]+)\s*</)" + std::string(tag) + R"(\s*>)");
      std::smatch match;
      if (!std::regex_search(block, match, pattern)) {
        return std::nullopt;
      }
      try {
        return std::stoi(match[1].str());
      } catch (const std::exception &) {
        return std::nullopt;
      }
    }
  }  // namespace

  std::optional<std::string> ensure_mode(std::string xml, mode_t mode) {
    if (xml.empty() || xml.size() > max_settings_bytes || mode.width < 320 || mode.width > 8192 || mode.height < 200 || mode.height > 4320 || mode.fps < 24 || mode.fps > 240) {
      return std::nullopt;
    }
    const auto clean = without_comments(xml);
    if (!clean) {
      return std::nullopt;
    }
    const auto open = clean->find("<resolutions>");
    const auto close = clean->find("</resolutions>", open == std::string::npos ? 0 : open + 13);
    if (open == std::string::npos || close == std::string::npos || clean->find("<vdd_settings>") == std::string::npos || clean->find("</vdd_settings>") == std::string::npos) {
      return std::nullopt;
    }
    const std::regex resolution_pattern(R"(<resolution\s*>([\s\S]*?)</resolution\s*>)");
    const std::regex global_pattern(R"(<g_refresh_rate\s*>\s*([0-9]+)\s*</g_refresh_rate\s*>)");
    bool resolution_exists = false;
    bool local_rate_exists = false;
    const std::string resolutions = clean->substr(open + 13, close - open - 13);
    for (auto it = std::sregex_iterator(resolutions.begin(), resolutions.end(), resolution_pattern); it != std::sregex_iterator(); ++it) {
      const auto block = (*it)[1].str();
      if (decimal_child(block, "width") == mode.width && decimal_child(block, "height") == mode.height) {
        resolution_exists = true;
        const std::regex rate_pattern(R"(<refresh_rate\s*>\s*([0-9]+)\s*</refresh_rate\s*>)");
        for (auto rate = std::sregex_iterator(block.begin(), block.end(), rate_pattern); rate != std::sregex_iterator(); ++rate) {
          try {
            local_rate_exists |= std::stoi((*rate)[1].str()) == mode.fps;
          } catch (const std::exception &) {
            return std::nullopt;
          }
        }
      }
    }
    bool global_rate_exists = false;
    for (auto it = std::sregex_iterator(clean->begin(), clean->end(), global_pattern); it != std::sregex_iterator(); ++it) {
      try {
        global_rate_exists |= std::stoi((*it)[1].str()) == mode.fps;
      } catch (const std::exception &) {
        return std::nullopt;
      }
    }
    if (resolution_exists && (local_rate_exists || global_rate_exists)) {
      return xml;
    }
    std::string added = "\n        <resolution><width>" + std::to_string(mode.width) + "</width><height>" + std::to_string(mode.height) + "</height>";
    if (!global_rate_exists) {
      added += "<refresh_rate>" + std::to_string(mode.fps) + "</refresh_rate>";
    }
    added += "</resolution>\n    ";
    xml.insert(close, added);
    return xml;
  }
}  // namespace managed_vdd
