// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include "ssc_tactile_hand_ros2_control/visibility_control.hpp"

struct gpiod_chip;
struct gpiod_line;

namespace ssc_tactile_hand_ros2_control
{

inline constexpr std::size_t kRaaCount = 9;
inline constexpr std::size_t kChannelsPerRaa = 6;
inline constexpr std::size_t kRawChannelCount = kRaaCount * kChannelsPerRaa;
inline constexpr std::array<std::size_t, kChannelsPerRaa> kChannelMap{{5, 0, 1, 2, 3, 4}};

using ChipSamples = std::array<uint16_t, kChannelsPerRaa>;
using RawFrame = std::array<ChipSamples, kRaaCount>;

enum class ResponseCheckMode
{
  kStrict,
  kLogOnly,
};

class SpiTransport
{
public:
  static constexpr std::size_t kInvalidIndex = std::numeric_limits<std::size_t>::max();

  struct ErrorContext
  {
    std::size_t chip{kInvalidIndex};
    std::size_t channel{kInvalidIndex};
    std::string message;
  };

  SSC_TACTILE_HAND_ROS2_CONTROL_PUBLIC
  SpiTransport(
    std::string spi_device, uint32_t speed_hz, uint8_t mode, std::string gpio_chip,
    std::array<unsigned int, kRaaCount> cs_line_offsets, std::size_t active_devices,
    std::chrono::microseconds measurement_wait, ResponseCheckMode response_check_mode);

  SSC_TACTILE_HAND_ROS2_CONTROL_PUBLIC
  ~SpiTransport();

  SpiTransport(const SpiTransport &) = delete;
  SpiTransport & operator=(const SpiTransport &) = delete;

  bool open();
  void close() noexcept;
  bool is_open() const noexcept;

  bool initialize_chip(std::size_t chip);
  bool measure_one_channel(
    std::size_t chip, std::size_t physical_channel, uint16_t & i_value, uint16_t & q_value);
  bool read_chip_channels(std::size_t chip, ChipSamples & i_values, ChipSamples & q_values);
  bool ensure_all_chip_selects_high() noexcept;

  const ErrorContext & last_error() const noexcept;

private:
  static constexpr uint8_t kBitsPerWord = 8;
  static constexpr uint8_t kInvalidChannel = 0xFF;
  static constexpr uint16_t kPostTransferDelayUs = 5;

  class CsHighGuard
  {
  public:
    explicit CsHighGuard(SpiTransport & owner) noexcept;
    ~CsHighGuard();
    void disarm() noexcept;

    CsHighGuard(const CsHighGuard &) = delete;
    CsHighGuard & operator=(const CsHighGuard &) = delete;

  private:
    SpiTransport & owner_;
    bool armed_{true};
  };

  bool open_gpio_lines();
  void close_gpio_lines() noexcept;
  bool deselect_all() noexcept;
  bool deselect_chip(std::size_t chip) noexcept;
  bool select_chip(std::size_t chip) noexcept;

  bool spi_xfer(std::size_t chip, const uint8_t * tx, uint8_t * rx, std::size_t length);
  bool tenaci_write(std::size_t chip, uint16_t register_address, uint16_t value);
  bool tenaci_read(
    std::size_t chip, uint16_t register_address,
    std::array<uint16_t, 2> & output_words);

  static uint16_t build_config_register(std::size_t physical_channel) noexcept;
  bool select_channel(std::size_t chip, std::size_t physical_channel);

  void set_error(std::size_t chip, std::size_t channel, std::string message);
  void annotate_channel_error(std::size_t chip, std::size_t channel);
  void log_response_mismatch_once(
    std::size_t chip, bool write, uint16_t register_address, const uint8_t * response,
    std::size_t response_size);

  std::string spi_device_;
  uint32_t speed_hz_;
  uint8_t mode_;
  std::string gpio_chip_path_;
  std::array<unsigned int, kRaaCount> cs_line_offsets_{};
  std::size_t active_devices_;
  std::chrono::microseconds measurement_wait_;
  ResponseCheckMode response_check_mode_;

  int spi_fd_{-1};
  gpiod_chip * gpio_chip_{nullptr};
  std::array<gpiod_line *, kRaaCount> cs_lines_{};
  std::array<uint8_t, kRaaCount> current_channel_{};
  std::array<bool, kRaaCount> write_mismatch_logged_{};
  std::array<bool, kRaaCount> read_mismatch_logged_{};
  ErrorContext last_error_{};
};

}  // namespace ssc_tactile_hand_ros2_control
