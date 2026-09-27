/**
 * @file src/vdd_modes.h
 * @brief Prepare a requested display mode in a VDD XML settings file before driver startup.
 */
#pragma once

#include <optional>
#include <string>

namespace managed_vdd {
  /**
   * @brief A Moonlight display mode requested for the first active stream.
   */
  struct mode_t {
    int width {};  ///< Horizontal pixel count.
    int height {};  ///< Vertical pixel count.
    int fps {};  ///< Integer refresh rate requested by the client.
  };

  /**
   * @brief Ensure that the VDD XML advertises a requested display mode.
   *
   * The original bytes are returned unchanged when the mode is already present.
   * A missing resolution is inserted before the closing resolutions element.
   * Existing comments, GPU selection and other settings are preserved byte for byte.
   *
   * @param xml Original VDD settings XML.
   * @param mode Requested width, height and refresh rate.
   * @return Updated XML, or no value when the input or request is unsupported.
   */
  std::optional<std::string> ensure_mode(std::string xml, mode_t mode);
}  // namespace managed_vdd
