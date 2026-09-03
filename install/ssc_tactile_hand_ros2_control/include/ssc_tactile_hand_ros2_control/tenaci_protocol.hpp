// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ssc_tactile_hand_ros2_control::raa2s4704
{

inline constexpr uint16_t kApplicationWriteCommand = 0x3000;
inline constexpr uint16_t kApplicationReadCommand = 0xC000;
inline constexpr uint16_t kRegisterAddressMask = 0x0FFF;

inline constexpr uint16_t kPowerConfigRegister = 0x0008;
inline constexpr uint16_t kMeasurementDriveStrengthRegister = 0x0011;
inline constexpr uint16_t kMeasurementConfig0Register = 0x0015;
inline constexpr uint16_t kMeasurementExecuteRegister = 0x0025;
inline constexpr uint16_t kMeasurementIResult0Register = 0x0214;
inline constexpr uint16_t kApplicationAuth0Register = 0x07F0;
inline constexpr uint16_t kApplicationAuth1Register = 0x07F1;

inline constexpr uint16_t kApplicationAuth0Key = 0xA10F;
inline constexpr uint16_t kApplicationAuth1Key = 0x4ECD;
inline constexpr uint16_t kPowerConfigValue = 0x001F;
inline constexpr uint16_t kMeasurementDriveStrengthValue = 0x0000;
inline constexpr uint16_t kSingleMeasurementExecuteValue = 0x0001;

inline constexpr std::size_t kOutOfFrameBytes = 2;
inline constexpr std::size_t kCommandAndDataBytes = 4;
inline constexpr std::size_t kCrcBytes = 2;
inline constexpr std::size_t kWriteFrameBytes = 8;
inline constexpr std::size_t kMeasurementResultWords = 2;
inline constexpr std::size_t kReadFrameBytes =
  kOutOfFrameBytes + kCommandAndDataBytes + (2 * kMeasurementResultWords) + kCrcBytes;

inline constexpr uint16_t kCrcPolynomial = 0x755B;

inline uint16_t crc16(const uint8_t * data, std::size_t size) noexcept
{
  uint16_t crc = 0;
  for (std::size_t index = 0; index < size; ++index) {
    crc = static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(data[index]) << 8));
    for (unsigned int bit = 0; bit < 8; ++bit) {
      const bool high_bit_set = (crc & 0x8000U) != 0U;
      crc = static_cast<uint16_t>(crc << 1);
      if (high_bit_set) {
        crc = static_cast<uint16_t>(crc ^ kCrcPolynomial);
      }
    }
  }
  return crc;
}

using WriteFrame = std::array<uint8_t, kWriteFrameBytes>;
using ReadFrame = std::array<uint8_t, kReadFrameBytes>;

inline WriteFrame make_write_frame(
  const uint16_t register_address, const uint16_t value) noexcept
{
  const uint16_t command = static_cast<uint16_t>(
    kApplicationWriteCommand | (register_address & kRegisterAddressMask));
  WriteFrame frame{};
  frame[0] = static_cast<uint8_t>(command >> 8);
  frame[1] = static_cast<uint8_t>(command);
  frame[2] = static_cast<uint8_t>(value >> 8);
  frame[3] = static_cast<uint8_t>(value);
  const uint16_t crc = crc16(frame.data(), kCommandAndDataBytes);
  frame[4] = static_cast<uint8_t>(crc >> 8);
  frame[5] = static_cast<uint8_t>(crc);
  return frame;
}

inline ReadFrame make_read_frame(
  const uint16_t register_address, const uint16_t word_count) noexcept
{
  const uint16_t command = static_cast<uint16_t>(
    kApplicationReadCommand | (register_address & kRegisterAddressMask));
  ReadFrame frame{};
  frame[0] = static_cast<uint8_t>(command >> 8);
  frame[1] = static_cast<uint8_t>(command);
  frame[2] = static_cast<uint8_t>(word_count >> 8);
  frame[3] = static_cast<uint8_t>(word_count);
  const uint16_t crc = crc16(frame.data(), kCommandAndDataBytes);
  frame[4] = static_cast<uint8_t>(crc >> 8);
  frame[5] = static_cast<uint8_t>(crc);
  return frame;
}

}  // namespace ssc_tactile_hand_ros2_control::raa2s4704
