// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "hardware_interface/handle.hpp"
#include "hardware_interface/hardware_info.hpp"
#include "hardware_interface/sensor_interface.hpp"
#include "hardware_interface/types/hardware_interface_return_values.hpp"
#include "rclcpp/macros.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/state.hpp"
#include "ssc_tactile_hand_ros2_control/spi_transport.hpp"
#include "ssc_tactile_hand_ros2_control/visibility_control.hpp"

namespace ssc_tactile_hand_ros2_control
{

class SscTactileHandHardwareInterface : public hardware_interface::SensorInterface
{
public:
  RCLCPP_SHARED_PTR_DEFINITIONS(SscTactileHandHardwareInterface)

  SSC_TACTILE_HAND_ROS2_CONTROL_PUBLIC
  hardware_interface::CallbackReturn on_init(
    const hardware_interface::HardwareComponentInterfaceParams & params) override;

  SSC_TACTILE_HAND_ROS2_CONTROL_PUBLIC
  std::vector<hardware_interface::StateInterface> export_state_interfaces() override;

  SSC_TACTILE_HAND_ROS2_CONTROL_PUBLIC
  hardware_interface::CallbackReturn on_activate(
    const rclcpp_lifecycle::State & previous_state) override;

  SSC_TACTILE_HAND_ROS2_CONTROL_PUBLIC
  hardware_interface::CallbackReturn on_deactivate(
    const rclcpp_lifecycle::State & previous_state) override;

  SSC_TACTILE_HAND_ROS2_CONTROL_PUBLIC
  hardware_interface::return_type read(
    const rclcpp::Time & time, const rclcpp::Duration & period) override;

private:
  using DoubleFrame = std::array<std::array<double, kChannelsPerRaa>, kRaaCount>;
  using EmaFrame = std::array<std::array<int32_t, kChannelsPerRaa>, kRaaCount>;
  using InitializedFrame = std::array<std::array<bool, kChannelsPerRaa>, kRaaCount>;

  struct SensorBinding
  {
    std::size_t chip{0};
    std::size_t logical_channel{0};
  };

  static constexpr std::size_t kStateInterfacesPerChannel = 3;
  static constexpr std::size_t kTareWarmupMeasurements = 5;
  static constexpr std::size_t kTareMeasurementAttempts = 20;
  static constexpr std::size_t kTimingSampleCount = 100;
  static constexpr int64_t kTimingBudgetNs = 25'000'000;

  bool parse_hardware_parameters();
  bool validate_sensor_layout();
  bool perform_tare_for_chip(std::size_t chip);
  bool prime_pipeline(std::size_t chip);

  void reset_runtime_state() noexcept;
  void reset_exported_state() noexcept;
  void reset_timing() noexcept;
  uint16_t update_ema(
    uint16_t sample, std::size_t chip, std::size_t logical_channel, EmaFrame & ema,
    InitializedFrame & initialized) const noexcept;
  uint16_t tare_for_output(std::size_t chip, std::size_t logical_channel) const noexcept;
  void log_transport_error(const char * operation, bool throttle);
  void record_timing(std::chrono::nanoseconds elapsed, const rclcpp::Duration & period);

  std::string spi_device_;
  uint32_t spi_speed_hz_{1500000};
  uint8_t spi_mode_{0};
  std::string gpio_chip_{"/dev/gpiochip1"};
  std::array<unsigned int, kRaaCount> cs_line_offsets_{{87, 80, 56, 60, 43, 68, 57, 61, 62}};
  std::size_t active_devices_{kRaaCount};
  bool auto_tare_{true};
  uint8_t ema_shift_{2};
  std::chrono::microseconds measurement_wait_{145};
  ResponseCheckMode response_check_mode_{ResponseCheckMode::kStrict};
  std::size_t max_consecutive_failures_{3};
  std::size_t recovery_interval_frames_{40};

  std::unique_ptr<SpiTransport> transport_;
  std::array<SensorBinding, kRawChannelCount> sensor_bindings_{};

  DoubleFrame raw_i_state_{};
  DoubleFrame raw_q_state_{};
  DoubleFrame value_state_{};
  EmaFrame ema_accumulator_{};
  InitializedFrame ema_initialized_{};
  RawFrame tare_physical_{};
  RawFrame last_valid_i_{};

  std::array<bool, kRaaCount> chip_online_{};
  std::array<std::size_t, kRaaCount> consecutive_failures_{};
  std::array<uint64_t, kRaaCount> total_failures_{};
  std::array<std::size_t, kRaaCount> recovery_countdown_{};

  bool active_{false};
  std::size_t timing_samples_{0};
  int64_t timing_total_ns_{0};
  int64_t timing_max_ns_{0};
  int64_t last_positive_period_ns_{0};
  std::chrono::steady_clock::time_point last_error_log_time_{};
};

}  // namespace ssc_tactile_hand_ros2_control
