// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

namespace ssc_tactile_hand_ros2_control
{

inline uint16_t ema_filter_step(
  const uint16_t sample, const uint8_t shift, int32_t & accumulator,
  bool & initialized) noexcept
{
  if (shift == 0) {
    return sample;
  }
  if (!initialized) {
    accumulator = static_cast<int32_t>(sample) << shift;
    initialized = true;
  }
  accumulator += static_cast<int32_t>(sample) - (accumulator >> shift);
  return static_cast<uint16_t>(accumulator >> shift);
}

inline constexpr uint16_t build_measurement_config(
  const std::size_t physical_channel) noexcept
{
  constexpr uint16_t average = 0;
  constexpr uint16_t pga_gain = 3;
  constexpr uint16_t sensor_gain = 3;
  constexpr uint16_t dac = 0;
  constexpr uint16_t frequency = 1;
  return static_cast<uint16_t>(
    ((average & 0x03U) << 14) | ((pga_gain & 0x03U) << 12) |
    ((sensor_gain & 0x03U) << 10) | ((dac & 0x03U) << 8) |
    ((frequency & 0x07U) << 4) | (physical_channel & 0x07U));
}

}  // namespace ssc_tactile_hand_ros2_control
