// SPDX-License-Identifier: Apache-2.0

#include "ssc_tactile_hand_ros2_control/spi_transport.hpp"

#include <errno.h>
#include <fcntl.h>
#include <gpiod.h>
#include <linux/spi/spidev.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <thread>
#include <utility>

#include "rclcpp/rclcpp.hpp"
#include "ssc_tactile_hand_ros2_control/tactile_processing.hpp"
#include "ssc_tactile_hand_ros2_control/tenaci_protocol.hpp"

namespace ssc_tactile_hand_ros2_control
{
namespace
{

std::string normalize_gpio_chip_path(const std::string & configured_path)
{
  if (!configured_path.empty() && configured_path.front() == '/') {
    return configured_path;
  }
  return "/dev/" + configured_path;
}

std::string errno_message(const std::string & operation)
{
  return operation + ": " + ::strerror(errno);
}

uint16_t unpack_u16(const uint8_t high, const uint8_t low) noexcept
{
  return static_cast<uint16_t>((static_cast<uint16_t>(high) << 8) | low);
}

}  // namespace

SpiTransport::SpiTransport(
  std::string spi_device, const uint32_t speed_hz, const uint8_t mode,
  std::string gpio_chip, std::array<unsigned int, kRaaCount> cs_line_offsets,
  const std::size_t active_devices, const std::chrono::microseconds measurement_wait,
  const ResponseCheckMode response_check_mode)
: spi_device_(std::move(spi_device)),
  speed_hz_(speed_hz),
  mode_(mode),
  gpio_chip_path_(normalize_gpio_chip_path(gpio_chip)),
  cs_line_offsets_(cs_line_offsets),
  active_devices_(active_devices),
  measurement_wait_(measurement_wait),
  response_check_mode_(response_check_mode)
{
  current_channel_.fill(kInvalidChannel);
  cs_lines_.fill(nullptr);
}

SpiTransport::~SpiTransport() { close(); }

SpiTransport::CsHighGuard::CsHighGuard(SpiTransport & owner) noexcept : owner_(owner) {}

SpiTransport::CsHighGuard::~CsHighGuard()
{
  if (armed_) {
    (void)owner_.deselect_all();
  }
}

void SpiTransport::CsHighGuard::disarm() noexcept { armed_ = false; }

bool SpiTransport::open()
{
  if (is_open()) {
    return true;
  }

  close();
  last_error_ = ErrorContext{};

  spi_fd_ = ::open(spi_device_.c_str(), O_RDWR | O_CLOEXEC);
  if (spi_fd_ < 0) {
    set_error(kInvalidIndex, kInvalidIndex, errno_message("open(" + spi_device_ + ")"));
    return false;
  }

  uint8_t configured_mode = mode_;
  if (::ioctl(spi_fd_, SPI_IOC_WR_MODE, &configured_mode) < 0) {
    set_error(kInvalidIndex, kInvalidIndex, errno_message("SPI_IOC_WR_MODE"));
    close();
    return false;
  }

  uint8_t least_significant_bit_first = 0;
  if (::ioctl(spi_fd_, SPI_IOC_WR_LSB_FIRST, &least_significant_bit_first) < 0) {
    set_error(kInvalidIndex, kInvalidIndex, errno_message("SPI_IOC_WR_LSB_FIRST"));
    close();
    return false;
  }

  uint8_t configured_bits = kBitsPerWord;
  if (::ioctl(spi_fd_, SPI_IOC_WR_BITS_PER_WORD, &configured_bits) < 0) {
    set_error(kInvalidIndex, kInvalidIndex, errno_message("SPI_IOC_WR_BITS_PER_WORD"));
    close();
    return false;
  }

  uint32_t configured_speed = speed_hz_;
  if (::ioctl(spi_fd_, SPI_IOC_WR_MAX_SPEED_HZ, &configured_speed) < 0) {
    set_error(kInvalidIndex, kInvalidIndex, errno_message("SPI_IOC_WR_MAX_SPEED_HZ"));
    close();
    return false;
  }

  if (!open_gpio_lines()) {
    close();
    return false;
  }

  current_channel_.fill(kInvalidChannel);
  write_mismatch_logged_.fill(false);
  read_mismatch_logged_.fill(false);
  return true;
}

void SpiTransport::close() noexcept
{
  (void)deselect_all();
  close_gpio_lines();
  if (spi_fd_ >= 0) {
    (void)::close(spi_fd_);
    spi_fd_ = -1;
  }
  current_channel_.fill(kInvalidChannel);
}

bool SpiTransport::is_open() const noexcept
{
  if (spi_fd_ < 0 || gpio_chip_ == nullptr) {
    return false;
  }
  for (std::size_t chip = 0; chip < kRaaCount; ++chip) {
    if (cs_lines_[chip] == nullptr) {
      return false;
    }
  }
  return true;
}

bool SpiTransport::open_gpio_lines()
{
  gpio_chip_ = ::gpiod_chip_open(gpio_chip_path_.c_str());
  if (gpio_chip_ == nullptr) {
    set_error(
      kInvalidIndex, kInvalidIndex, errno_message("gpiod_chip_open(" + gpio_chip_path_ + ")"));
    return false;
  }

  for (std::size_t chip = 0; chip < kRaaCount; ++chip) {
    gpiod_line * line = ::gpiod_chip_get_line(gpio_chip_, cs_line_offsets_[chip]);
    if (line == nullptr) {
      std::ostringstream message;
      message << "gpiod_chip_get_line(offset=" << cs_line_offsets_[chip] << "): "
              << ::strerror(errno);
      set_error(chip, kInvalidIndex, message.str());
      return false;
    }

    if (::gpiod_line_request_output(line, "ssc_tactile_hand", 1) < 0) {
      std::ostringstream message;
      message << "gpiod_line_request_output(offset=" << cs_line_offsets_[chip] << "): "
              << ::strerror(errno);
      set_error(chip, kInvalidIndex, message.str());
      return false;
    }
    cs_lines_[chip] = line;
  }

  if (!deselect_all()) {
    set_error(kInvalidIndex, kInvalidIndex, "failed to drive every requested CS line HIGH");
    return false;
  }
  return true;
}

void SpiTransport::close_gpio_lines() noexcept
{
  for (std::size_t chip = 0; chip < kRaaCount; ++chip) {
    if (cs_lines_[chip] != nullptr) {
      ::gpiod_line_release(cs_lines_[chip]);
      cs_lines_[chip] = nullptr;
    }
  }
  if (gpio_chip_ != nullptr) {
    ::gpiod_chip_close(gpio_chip_);
    gpio_chip_ = nullptr;
  }
}

bool SpiTransport::deselect_all() noexcept
{
  bool all_high = true;
  for (std::size_t chip = 0; chip < kRaaCount; ++chip) {
    if (cs_lines_[chip] != nullptr && ::gpiod_line_set_value(cs_lines_[chip], 1) < 0) {
      all_high = false;
    }
  }
  return all_high;
}

bool SpiTransport::select_chip(const std::size_t chip) noexcept
{
  if (chip >= active_devices_ || cs_lines_[chip] == nullptr) {
    return false;
  }
  return ::gpiod_line_set_value(cs_lines_[chip], 0) == 0;
}

bool SpiTransport::deselect_chip(const std::size_t chip) noexcept
{
  return chip < active_devices_ && cs_lines_[chip] != nullptr &&
         ::gpiod_line_set_value(cs_lines_[chip], 1) == 0;
}

bool SpiTransport::spi_xfer(
  const std::size_t chip, const uint8_t * const tx, uint8_t * const rx,
  const std::size_t length)
{
  CsHighGuard restore_high(*this);

  if (spi_fd_ < 0 || tx == nullptr || rx == nullptr || length == 0) {
    set_error(chip, kInvalidIndex, "invalid SPI transfer state or buffer");
    return false;
  }
  if (chip >= active_devices_) {
    set_error(chip, kInvalidIndex, "chip index is outside active_devices");
    return false;
  }
  if (!select_chip(chip)) {
    std::ostringstream message;
    message << "failed to drive CS LOW for GPIO offset " << cs_line_offsets_[chip];
    set_error(chip, kInvalidIndex, message.str());
    return false;
  }

  spi_ioc_transfer transfer{};
  transfer.tx_buf = static_cast<__u64>(reinterpret_cast<std::uintptr_t>(tx));
  transfer.rx_buf = static_cast<__u64>(reinterpret_cast<std::uintptr_t>(rx));
  transfer.len = static_cast<__u32>(length);
  transfer.speed_hz = speed_hz_;
  transfer.delay_usecs = kPostTransferDelayUs;
  transfer.bits_per_word = kBitsPerWord;
  transfer.cs_change = 0;

  const int transferred = ::ioctl(spi_fd_, SPI_IOC_MESSAGE(1), &transfer);
  if (transferred < 0) {
    set_error(chip, kInvalidIndex, errno_message("SPI_IOC_MESSAGE(1)"));
    return false;
  }
  if (transferred != static_cast<int>(length)) {
    std::ostringstream message;
    message << "short SPI transfer: expected " << length << " bytes, got " << transferred;
    set_error(chip, kInvalidIndex, message.str());
    return false;
  }

  if (!deselect_chip(chip)) {
    set_error(
      chip, kInvalidIndex, "failed to restore the selected CS line HIGH after SPI transfer");
    return false;
  }
  restore_high.disarm();
  return true;
}

bool SpiTransport::tenaci_write(
  const std::size_t chip, const uint16_t register_address, const uint16_t value)
{
  const auto tx = raa2s4704::make_write_frame(register_address, value);
  std::array<uint8_t, raa2s4704::kWriteFrameBytes> rx{};

  if (!spi_xfer(chip, tx.data(), rx.data(), tx.size())) {
    return false;
  }

  const auto response_start = rx.begin() + static_cast<std::ptrdiff_t>(raa2s4704::kOutOfFrameBytes);
  const bool echo_matches = std::equal(tx.begin(), tx.begin() + 4, response_start);
  const uint16_t request_crc = unpack_u16(tx[4], tx[5]);
  const uint16_t response_crc = unpack_u16(rx[6], rx[7]);
  const uint16_t calculated_crc =
    raa2s4704::crc16(rx.data() + raa2s4704::kOutOfFrameBytes, 4);
  const bool crc_matches = response_crc == calculated_crc && response_crc == request_crc;
  if (!echo_matches || !crc_matches) {
    if (response_check_mode_ == ResponseCheckMode::kLogOnly) {
      log_response_mismatch_once(chip, true, register_address, rx.data(), rx.size());
      return true;
    }
    std::ostringstream message;
    message << "RAA write " << (echo_matches ? "CRC" : "echo")
            << " mismatch at register 0x" << std::hex << register_address;
    set_error(chip, kInvalidIndex, message.str());
    return false;
  }
  return true;
}

bool SpiTransport::tenaci_read(
  const std::size_t chip, const uint16_t register_address,
  std::array<uint16_t, 2> & output_words)
{
  const auto tx = raa2s4704::make_read_frame(
    register_address, raa2s4704::kMeasurementResultWords);
  std::array<uint8_t, raa2s4704::kReadFrameBytes> rx{};

  if (!spi_xfer(chip, tx.data(), rx.data(), tx.size())) {
    return false;
  }

  const auto response_start = rx.begin() + static_cast<std::ptrdiff_t>(raa2s4704::kOutOfFrameBytes);
  const bool echo_matches = std::equal(tx.begin(), tx.begin() + 4, response_start);

  constexpr std::size_t response_crc_index = raa2s4704::kReadFrameBytes - raa2s4704::kCrcBytes;
  const uint16_t response_crc = unpack_u16(rx[response_crc_index], rx[response_crc_index + 1]);
  const uint16_t calculated_crc = raa2s4704::crc16(
    rx.data() + raa2s4704::kOutOfFrameBytes,
    raa2s4704::kCommandAndDataBytes + (2 * raa2s4704::kMeasurementResultWords));
  if (!echo_matches || response_crc != calculated_crc) {
    if (response_check_mode_ == ResponseCheckMode::kLogOnly) {
      log_response_mismatch_once(chip, false, register_address, rx.data(), rx.size());
    } else {
      std::ostringstream message;
      message << "RAA read " << (echo_matches ? "CRC" : "echo")
              << " mismatch at register 0x" << std::hex << register_address;
      set_error(chip, kInvalidIndex, message.str());
      return false;
    }
  }

  constexpr std::size_t data_start =
    raa2s4704::kOutOfFrameBytes + raa2s4704::kCommandAndDataBytes;
  for (std::size_t word = 0; word < output_words.size(); ++word) {
    output_words[word] = unpack_u16(rx[data_start + (2 * word)], rx[data_start + (2 * word) + 1]);
  }
  return true;
}

void SpiTransport::log_response_mismatch_once(
  const std::size_t chip, const bool write, const uint16_t register_address,
  const uint8_t * const response, const std::size_t response_size)
{
  auto & already_logged = write ? write_mismatch_logged_[chip] : read_mismatch_logged_[chip];
  if (already_logged) {
    return;
  }
  already_logged = true;

  std::ostringstream bytes;
  bytes << std::hex << std::setfill('0');
  for (std::size_t index = 0; index < response_size; ++index) {
    if (index > 0) {
      bytes << ' ';
    }
    bytes << std::setw(2) << static_cast<unsigned int>(response[index]);
  }
  RCLCPP_WARN(
    rclcpp::get_logger("ssc_tactile_hand_spi_transport"),
    "response_check=log-only accepted chip %zu %s response mismatch at register 0x%04x; "
    "raw rx=[%s]", chip, write ? "write" : "read", register_address, bytes.str().c_str());
}

uint16_t SpiTransport::build_config_register(const std::size_t physical_channel) noexcept
{
  return build_measurement_config(physical_channel);
}

bool SpiTransport::initialize_chip(const std::size_t chip)
{
  if (chip >= active_devices_) {
    set_error(chip, kInvalidIndex, "chip index is outside active_devices");
    return false;
  }

  current_channel_[chip] = kInvalidChannel;
  if (!tenaci_write(
        chip, raa2s4704::kApplicationAuth0Register, raa2s4704::kApplicationAuth0Key)) {
    return false;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  if (!tenaci_write(
        chip, raa2s4704::kApplicationAuth1Register, raa2s4704::kApplicationAuth1Key)) {
    return false;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  if (!tenaci_write(chip, raa2s4704::kMeasurementConfig0Register, build_config_register(0))) {
    return false;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  if (!tenaci_write(
        chip, raa2s4704::kMeasurementDriveStrengthRegister,
        raa2s4704::kMeasurementDriveStrengthValue)) {
    return false;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  if (!tenaci_write(chip, raa2s4704::kPowerConfigRegister, raa2s4704::kPowerConfigValue)) {
    return false;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  current_channel_[chip] = kInvalidChannel;
  return true;
}

bool SpiTransport::select_channel(
  const std::size_t chip, const std::size_t physical_channel)
{
  if (chip >= active_devices_ || physical_channel >= kChannelsPerRaa) {
    set_error(chip, physical_channel, "invalid chip or physical channel index");
    return false;
  }
  if (current_channel_[chip] == physical_channel) {
    return true;
  }
  if (!tenaci_write(
        chip, raa2s4704::kMeasurementConfig0Register,
        build_config_register(physical_channel))) {
    annotate_channel_error(chip, physical_channel);
    return false;
  }
  current_channel_[chip] = static_cast<uint8_t>(physical_channel);
  return true;
}

bool SpiTransport::measure_one_channel(
  const std::size_t chip, const std::size_t physical_channel, uint16_t & i_value,
  uint16_t & q_value)
{
  if (!select_channel(chip, physical_channel)) {
    return false;
  }
  if (!tenaci_write(
        chip, raa2s4704::kMeasurementExecuteRegister,
        raa2s4704::kSingleMeasurementExecuteValue)) {
    annotate_channel_error(chip, physical_channel);
    return false;
  }

  std::this_thread::sleep_for(measurement_wait_);

  std::array<uint16_t, 2> iq{};
  if (!tenaci_read(chip, raa2s4704::kMeasurementIResult0Register, iq)) {
    annotate_channel_error(chip, physical_channel);
    return false;
  }
  i_value = iq[0];
  q_value = iq[1];
  return true;
}

bool SpiTransport::read_chip_channels(
  const std::size_t chip, ChipSamples & i_values, ChipSamples & q_values)
{
  for (std::size_t physical_channel = 0; physical_channel < kChannelsPerRaa;
       ++physical_channel) {
    if (!measure_one_channel(
          chip, physical_channel, i_values[physical_channel], q_values[physical_channel])) {
      return false;
    }
  }
  return true;
}

bool SpiTransport::ensure_all_chip_selects_high() noexcept { return deselect_all(); }

const SpiTransport::ErrorContext & SpiTransport::last_error() const noexcept
{
  return last_error_;
}

void SpiTransport::set_error(
  const std::size_t chip, const std::size_t channel, std::string message)
{
  last_error_.chip = chip;
  last_error_.channel = channel;
  last_error_.message = std::move(message);
}

void SpiTransport::annotate_channel_error(
  const std::size_t chip, const std::size_t channel)
{
  last_error_.chip = chip;
  last_error_.channel = channel;
}

}  // namespace ssc_tactile_hand_ros2_control
