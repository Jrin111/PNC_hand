// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cstdint>
#include <iostream>
#include <string>

#include "ssc_tactile_hand_ros2_control/spi_transport.hpp"
#include "ssc_tactile_hand_ros2_control/tactile_processing.hpp"
#include "ssc_tactile_hand_ros2_control/tenaci_protocol.hpp"

namespace tactile = ssc_tactile_hand_ros2_control;
namespace protocol = ssc_tactile_hand_ros2_control::raa2s4704;

namespace
{

template<typename Actual, typename Expected>
bool expect_equal(const Actual & actual, const Expected & expected, const std::string & label)
{
  if (actual == expected) {
    return true;
  }
  std::cerr << "FAIL: " << label << '\n';
  return false;
}

}  // namespace

int main()
{
  bool passed = true;

  constexpr std::array<uint8_t, 4> auth0_payload{{0x37, 0xF0, 0xA1, 0x0F}};
  constexpr std::array<uint8_t, 4> read_payload{{0xC2, 0x14, 0x00, 0x02}};
  passed &= expect_equal(protocol::crc16(nullptr, 0), uint16_t{0x0000}, "empty CRC vector");
  passed &= expect_equal(
    protocol::crc16(auth0_payload.data(), auth0_payload.size()), uint16_t{0x1ED3},
    "AUTH0 CRC vector");
  passed &= expect_equal(
    protocol::crc16(read_payload.data(), read_payload.size()), uint16_t{0x1CC8},
    "measurement-read CRC vector");

  const protocol::WriteFrame expected_write{{
    0x37, 0xF0, 0xA1, 0x0F, 0x1E, 0xD3, 0x00, 0x00}};
  passed &= expect_equal(
    protocol::make_write_frame(protocol::kApplicationAuth0Register, protocol::kApplicationAuth0Key),
    expected_write, "AUTH0 write frame");

  const protocol::ReadFrame expected_read{{
    0xC2, 0x14, 0x00, 0x02, 0x1C, 0xC8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
  passed &= expect_equal(
    protocol::make_read_frame(
      protocol::kMeasurementIResult0Register, protocol::kMeasurementResultWords),
    expected_read, "I/Q read frame");

  constexpr std::array<std::size_t, tactile::kChannelsPerRaa> expected_map{{5, 0, 1, 2, 3, 4}};
  passed &= expect_equal(tactile::kChannelMap, expected_map, "firmware channel map");
  passed &= expect_equal(
    tactile::build_measurement_config(0), uint16_t{0x3C10}, "channel 0 config");
  passed &= expect_equal(
    tactile::build_measurement_config(5), uint16_t{0x3C15}, "channel 5 config");

  int32_t accumulator = 0;
  bool initialized = false;
  passed &= expect_equal(
    tactile::ema_filter_step(100, 2, accumulator, initialized), uint16_t{100}, "EMA seed");
  passed &= expect_equal(
    tactile::ema_filter_step(104, 2, accumulator, initialized), uint16_t{101}, "EMA update");
  passed &= expect_equal(
    tactile::ema_filter_step(321, 0, accumulator, initialized), uint16_t{321},
    "EMA shift zero bypass");

  if (!passed) {
    return 1;
  }
  std::cout << "protocol and processing checks passed\n";
  return 0;
}
