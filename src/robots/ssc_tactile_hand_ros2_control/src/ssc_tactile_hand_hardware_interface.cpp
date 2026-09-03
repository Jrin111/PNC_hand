// SPDX-License-Identifier: Apache-2.0

#include "ssc_tactile_hand_ros2_control/ssc_tactile_hand_hardware_interface.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "pluginlib/class_list_macros.hpp"
#include "ssc_tactile_hand_ros2_control/tactile_processing.hpp"

namespace ssc_tactile_hand_ros2_control
{
namespace
{

constexpr char kLoggerName[] = "ssc_tactile_hand_hardware_interface";
constexpr char kDefaultCsLines[] = "87,80,56,60,43,68,57,61,62";

std::string_view trim(const std::string_view text) noexcept
{
  const std::size_t first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  const std::size_t last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, (last - first) + 1);
}

bool parse_unsigned(
  const std::string_view text, const uint64_t minimum, const uint64_t maximum,
  uint64_t & output) noexcept
{
  const std::string_view stripped = trim(text);
  if (stripped.empty()) {
    return false;
  }

  uint64_t parsed = 0;
  const auto result = std::from_chars(
    stripped.data(), stripped.data() + stripped.size(), parsed, 10);
  if (result.ec != std::errc{} || result.ptr != stripped.data() + stripped.size() ||
    parsed < minimum || parsed > maximum)
  {
    return false;
  }
  output = parsed;
  return true;
}

bool parse_boolean(const std::string_view text, bool & output) noexcept
{
  const std::string_view stripped = trim(text);
  if (stripped == "true" || stripped == "1") {
    output = true;
    return true;
  }
  if (stripped == "false" || stripped == "0") {
    output = false;
    return true;
  }
  return false;
}

}  // namespace

hardware_interface::CallbackReturn SscTactileHandHardwareInterface::on_init(
  const hardware_interface::HardwareComponentInterfaceParams & params)
{
  if (hardware_interface::SensorInterface::on_init(params) != CallbackReturn::SUCCESS) {
    return CallbackReturn::ERROR;
  }

  reset_runtime_state();
  if (!parse_hardware_parameters() || !validate_sensor_layout()) {
    return CallbackReturn::ERROR;
  }

  transport_ = std::make_unique<SpiTransport>(
    spi_device_, spi_speed_hz_, spi_mode_, gpio_chip_, cs_line_offsets_, active_devices_,
    measurement_wait_, response_check_mode_);
  return CallbackReturn::SUCCESS;
}

bool SscTactileHandHardwareInterface::parse_hardware_parameters()
{
  const auto logger = rclcpp::get_logger(kLoggerName);
  const auto & parameters = info_.hardware_parameters;

  const auto parameter_or = [&parameters](
                              const std::string & name,
                              const std::string & fallback) -> std::string {
      const auto iterator = parameters.find(name);
      return iterator == parameters.end() ? fallback : iterator->second;
    };

  const auto spi_device_iterator = parameters.find("spi_device");
  if (spi_device_iterator == parameters.end() || trim(spi_device_iterator->second).empty()) {
    RCLCPP_ERROR(logger, "hardware parameter 'spi_device' is required and must not be empty");
    return false;
  }
  spi_device_ = std::string(trim(spi_device_iterator->second));

  uint64_t parsed = 0;
  const std::string speed_text = parameter_or("spi_speed_hz", "1500000");
  if (!parse_unsigned(speed_text, 1, 2000000, parsed)) {
    RCLCPP_ERROR(
      logger, "spi_speed_hz must be in [1, 2000000], got '%s'", speed_text.c_str());
    return false;
  }
  spi_speed_hz_ = static_cast<uint32_t>(parsed);

  const std::string mode_text = parameter_or("spi_mode", "0");
  if (!parse_unsigned(mode_text, 0, 0, parsed)) {
    RCLCPP_ERROR(logger, "spi_mode must be 0, got '%s'", mode_text.c_str());
    return false;
  }
  spi_mode_ = static_cast<uint8_t>(parsed);

  const std::string bits_text = parameter_or("bits_per_word", "8");
  if (!parse_unsigned(bits_text, 8, 8, parsed)) {
    RCLCPP_ERROR(logger, "bits_per_word must be 8, got '%s'", bits_text.c_str());
    return false;
  }

  gpio_chip_ = std::string(trim(parameter_or("gpio_chip", "/dev/gpiochip1")));
  if (gpio_chip_.empty()) {
    RCLCPP_ERROR(logger, "gpio_chip must not be empty");
    return false;
  }

  const std::string active_text = parameter_or("active_devices", "9");
  if (!parse_unsigned(active_text, 1, kRaaCount, parsed)) {
    RCLCPP_ERROR(logger, "active_devices must be in [1, 9], got '%s'", active_text.c_str());
    return false;
  }
  active_devices_ = static_cast<std::size_t>(parsed);

  const std::string channels_text = parameter_or("channels_per_device", "6");
  if (!parse_unsigned(channels_text, kChannelsPerRaa, kChannelsPerRaa, parsed)) {
    RCLCPP_ERROR(
      logger, "channels_per_device must be 6, got '%s'", channels_text.c_str());
    return false;
  }

  const std::string auto_tare_text = parameter_or("auto_tare", "true");
  if (!parse_boolean(auto_tare_text, auto_tare_)) {
    RCLCPP_ERROR(
      logger, "auto_tare must be true or false, got '%s'", auto_tare_text.c_str());
    return false;
  }

  const std::string ema_shift_text = parameter_or("ema_shift", "2");
  if (!parse_unsigned(ema_shift_text, 0, 15, parsed)) {
    RCLCPP_ERROR(logger, "ema_shift must be in [0, 15], got '%s'", ema_shift_text.c_str());
    return false;
  }
  ema_shift_ = static_cast<uint8_t>(parsed);

  const std::string wait_text = parameter_or("measurement_wait_us", "145");
  if (!parse_unsigned(wait_text, 1, 1000000, parsed)) {
    RCLCPP_ERROR(
      logger, "measurement_wait_us must be in [1, 1000000], got '%s'", wait_text.c_str());
    return false;
  }
  measurement_wait_ = std::chrono::microseconds(static_cast<int64_t>(parsed));

  const std::string response_check_text = parameter_or("response_check", "strict");
  if (response_check_text == "strict") {
    response_check_mode_ = ResponseCheckMode::kStrict;
  } else if (response_check_text == "log-only") {
    response_check_mode_ = ResponseCheckMode::kLogOnly;
  } else {
    RCLCPP_ERROR(
      logger, "response_check must be 'strict' or 'log-only', got '%s'",
      response_check_text.c_str());
    return false;
  }

  const std::string failure_limit_text = parameter_or("max_consecutive_failures", "3");
  if (!parse_unsigned(failure_limit_text, 1, 1000, parsed)) {
    RCLCPP_ERROR(
      logger, "max_consecutive_failures must be in [1, 1000], got '%s'",
      failure_limit_text.c_str());
    return false;
  }
  max_consecutive_failures_ = static_cast<std::size_t>(parsed);

  const std::string recovery_interval_text = parameter_or("recovery_interval_frames", "40");
  if (!parse_unsigned(recovery_interval_text, 1, 1000000, parsed)) {
    RCLCPP_ERROR(
      logger, "recovery_interval_frames must be in [1, 1000000], got '%s'",
      recovery_interval_text.c_str());
    return false;
  }
  recovery_interval_frames_ = static_cast<std::size_t>(parsed);

  const std::string cs_text = parameter_or("cs_lines", kDefaultCsLines);
  std::size_t offset_count = 0;
  std::size_t begin = 0;
  while (begin <= cs_text.size()) {
    const std::size_t comma = cs_text.find(',', begin);
    const std::size_t end = comma == std::string::npos ? cs_text.size() : comma;
    if (offset_count >= kRaaCount ||
      !parse_unsigned(
        std::string_view(cs_text).substr(begin, end - begin), 0,
        std::numeric_limits<unsigned int>::max(), parsed))
    {
      RCLCPP_ERROR(
        logger, "cs_lines must contain exactly 9 comma-separated unsigned offsets; got '%s'",
        cs_text.c_str());
      return false;
    }
    cs_line_offsets_[offset_count] = static_cast<unsigned int>(parsed);
    ++offset_count;
    if (comma == std::string::npos) {
      break;
    }
    begin = comma + 1;
  }
  if (offset_count != kRaaCount) {
    RCLCPP_ERROR(
      logger, "cs_lines must contain exactly 9 comma-separated offsets; got %zu", offset_count);
    return false;
  }
  for (std::size_t left = 0; left < kRaaCount; ++left) {
    for (std::size_t right = left + 1; right < kRaaCount; ++right) {
      if (cs_line_offsets_[left] == cs_line_offsets_[right]) {
        RCLCPP_ERROR(
          logger, "cs_lines contains duplicate offset %u", cs_line_offsets_[left]);
        return false;
      }
    }
  }
  return true;
}

bool SscTactileHandHardwareInterface::validate_sensor_layout()
{
  const auto logger = rclcpp::get_logger(kLoggerName);
  if (info_.sensors.size() != kRawChannelCount) {
    RCLCPP_ERROR(
      logger, "expected exactly %zu sensors, got %zu", kRawChannelCount, info_.sensors.size());
    return false;
  }

  std::array<std::array<bool, kChannelsPerRaa>, kRaaCount> seen{};
  for (std::size_t sensor_index = 0; sensor_index < info_.sensors.size(); ++sensor_index) {
    const auto & sensor = info_.sensors[sensor_index];
    bool matched = false;
    for (std::size_t chip = 0; chip < kRaaCount && !matched; ++chip) {
      for (std::size_t channel = 0; channel < kChannelsPerRaa; ++channel) {
        const std::string expected_name =
          "raa" + std::to_string(chip) + "_ch" + std::to_string(channel);
        if (sensor.name != expected_name) {
          continue;
        }
        if (seen[chip][channel]) {
          RCLCPP_ERROR(logger, "duplicate sensor '%s'", sensor.name.c_str());
          return false;
        }
        seen[chip][channel] = true;
        sensor_bindings_[sensor_index] = SensorBinding{chip, channel};
        matched = true;
        break;
      }
    }
    if (!matched) {
      RCLCPP_ERROR(
        logger, "unexpected sensor '%s'; expected names raa0_ch0 through raa8_ch5",
        sensor.name.c_str());
      return false;
    }

    if (sensor.state_interfaces.size() != kStateInterfacesPerChannel) {
      RCLCPP_ERROR(
        logger, "sensor '%s' must declare raw_i, raw_q, and value", sensor.name.c_str());
      return false;
    }
    bool has_raw_i = false;
    bool has_raw_q = false;
    bool has_value = false;
    for (const auto & interface : sensor.state_interfaces) {
      if (interface.name == "raw_i" && !has_raw_i) {
        has_raw_i = true;
      } else if (interface.name == "raw_q" && !has_raw_q) {
        has_raw_q = true;
      } else if (interface.name == "value" && !has_value) {
        has_value = true;
      } else {
        RCLCPP_ERROR(
          logger, "sensor '%s' has an unknown or duplicate state interface '%s'",
          sensor.name.c_str(), interface.name.c_str());
        return false;
      }
    }
  }
  return true;
}

std::vector<hardware_interface::StateInterface>
SscTactileHandHardwareInterface::export_state_interfaces()
{
  std::vector<hardware_interface::StateInterface> interfaces;
  interfaces.reserve(kRawChannelCount * kStateInterfacesPerChannel);
  for (std::size_t sensor_index = 0; sensor_index < info_.sensors.size(); ++sensor_index) {
    const auto & sensor = info_.sensors[sensor_index];
    const SensorBinding binding = sensor_bindings_[sensor_index];
    for (const auto & interface : sensor.state_interfaces) {
      double * value = nullptr;
      if (interface.name == "raw_i") {
        value = &raw_i_state_[binding.chip][binding.logical_channel];
      } else if (interface.name == "raw_q") {
        value = &raw_q_state_[binding.chip][binding.logical_channel];
      } else {
        value = &value_state_[binding.chip][binding.logical_channel];
      }
      interfaces.emplace_back(sensor.name, interface.name, value);
    }
  }
  return interfaces;
}

hardware_interface::CallbackReturn SscTactileHandHardwareInterface::on_activate(
  const rclcpp_lifecycle::State & previous_state)
{
  (void)previous_state;
  const auto logger = rclcpp::get_logger(kLoggerName);
  active_ = false;
  reset_runtime_state();
  if (!transport_) {
    RCLCPP_ERROR(logger, "SPI transport was not created during initialization");
    return CallbackReturn::ERROR;
  }
  if (!transport_->open()) {
    log_transport_error("open", false);
    transport_->close();
    return CallbackReturn::ERROR;
  }

  std::size_t online_count = 0;
  for (std::size_t chip = 0; chip < active_devices_; ++chip) {
    bool ready = transport_->initialize_chip(chip);
    if (!ready) {
      ++total_failures_[chip];
      log_transport_error("initialize", false);
    } else if (auto_tare_) {
      ready = perform_tare_for_chip(chip);
    } else {
      ready = prime_pipeline(chip);
    }

    if (!transport_->ensure_all_chip_selects_high()) {
      RCLCPP_ERROR(logger, "cannot guarantee that every chip-select line is HIGH");
      transport_->close();
      return CallbackReturn::ERROR;
    }

    chip_online_[chip] = ready;
    if (ready) {
      ++online_count;
    } else {
      recovery_countdown_[chip] = recovery_interval_frames_;
      RCLCPP_WARN(
        logger, "chip %zu is unavailable during activation and will be retried every %zu frames",
        chip, recovery_interval_frames_);
    }
  }
  if (online_count == 0) {
    RCLCPP_ERROR(logger, "no RAA device completed initialization; activation refused");
    transport_->close();
    return CallbackReturn::ERROR;
  }

  reset_exported_state();
  reset_timing();
  active_ = true;
  RCLCPP_INFO(
    logger, "activated %zu of %zu configured RAA devices on %s at %u Hz", online_count,
    active_devices_, spi_device_.c_str(), spi_speed_hz_);
  return CallbackReturn::SUCCESS;
}

hardware_interface::CallbackReturn SscTactileHandHardwareInterface::on_deactivate(
  const rclcpp_lifecycle::State & previous_state)
{
  (void)previous_state;
  active_ = false;
  if (transport_) {
    transport_->close();
  }
  reset_exported_state();
  return CallbackReturn::SUCCESS;
}

bool SscTactileHandHardwareInterface::perform_tare_for_chip(const std::size_t chip)
{
  for (std::size_t physical_channel = 0; physical_channel < kChannelsPerRaa;
    ++physical_channel)
  {
    uint16_t i_value = 0;
    uint16_t q_value = 0;
    for (std::size_t warmup = 0; warmup < kTareWarmupMeasurements; ++warmup) {
      if (!transport_->measure_one_channel(chip, physical_channel, i_value, q_value)) {
        ++total_failures_[chip];
        log_transport_error("tare warm-up", true);
      }
    }

    uint64_t sum = 0;
    std::size_t valid_count = 0;
    std::size_t successful_samples = 0;
    for (std::size_t attempt = 0; attempt < kTareMeasurementAttempts; ++attempt) {
      if (transport_->measure_one_channel(chip, physical_channel, i_value, q_value)) {
        ++successful_samples;
        if (i_value > 0) {
          sum += i_value;
          ++valid_count;
        }
      } else {
        ++total_failures_[chip];
        log_transport_error("tare sample", true);
      }
    }

    if (successful_samples == 0) {
      return false;
    }

    const uint16_t baseline = valid_count == 0 ? 0 : static_cast<uint16_t>(sum / valid_count);
    tare_physical_[chip][physical_channel] = baseline;
    ema_accumulator_[chip][physical_channel] = static_cast<int32_t>(baseline) << ema_shift_;
    ema_initialized_[chip][physical_channel] = true;
    last_valid_i_[chip][physical_channel] = baseline;
  }
  return true;
}

bool SscTactileHandHardwareInterface::prime_pipeline(const std::size_t chip)
{
  uint16_t ignored_i = 0;
  uint16_t ignored_q = 0;
  if (transport_->measure_one_channel(chip, kChannelsPerRaa - 1, ignored_i, ignored_q)) {
    return true;
  }
  ++total_failures_[chip];
  log_transport_error("pipeline prime", true);
  return false;
}

hardware_interface::return_type SscTactileHandHardwareInterface::read(
  const rclcpp::Time & time, const rclcpp::Duration & period)
{
  (void)time;
  if (!active_ || !transport_ || !transport_->is_open()) {
    if (transport_) {
      (void)transport_->ensure_all_chip_selects_high();
    }
    return hardware_interface::return_type::ERROR;
  }

  const auto frame_start = std::chrono::steady_clock::now();
  RawFrame staged_i{};
  RawFrame staged_q{};
  std::array<bool, kRaaCount> sample_valid{};
  bool recovery_attempted = false;
  const auto logger = rclcpp::get_logger(kLoggerName);
  for (std::size_t chip = 0; chip < active_devices_; ++chip) {
    if (!chip_online_[chip]) {
      if (recovery_countdown_[chip] > 0) {
        --recovery_countdown_[chip];
        continue;
      }
      if (recovery_attempted) {
        continue;
      }
      recovery_attempted = true;
      RCLCPP_INFO(logger, "attempting recovery of isolated chip %zu", chip);

      bool recovered = transport_->initialize_chip(chip);
      if (!recovered) {
        ++total_failures_[chip];
        log_transport_error("recovery initialize", true);
      } else if (auto_tare_) {
        recovered = perform_tare_for_chip(chip);
      } else {
        recovered = prime_pipeline(chip);
      }

      if (!transport_->ensure_all_chip_selects_high()) {
        RCLCPP_ERROR(logger, "cannot guarantee that every chip-select line is HIGH");
        active_ = false;
        return hardware_interface::return_type::ERROR;
      }
      if (!recovered) {
        recovery_countdown_[chip] = recovery_interval_frames_;
        continue;
      }

      chip_online_[chip] = true;
      consecutive_failures_[chip] = 0;
      RCLCPP_INFO(
        logger, "chip %zu recovered after %llu transport failures", chip,
        static_cast<unsigned long long>(total_failures_[chip]));
    }

    if (!transport_->read_chip_channels(chip, staged_i[chip], staged_q[chip])) {
      if (!transport_->ensure_all_chip_selects_high()) {
        RCLCPP_ERROR(logger, "cannot guarantee that every chip-select line is HIGH");
        active_ = false;
        return hardware_interface::return_type::ERROR;
      }
      ++consecutive_failures_[chip];
      ++total_failures_[chip];
      log_transport_error("read", true);
      if (consecutive_failures_[chip] >= max_consecutive_failures_) {
        chip_online_[chip] = false;
        recovery_countdown_[chip] = recovery_interval_frames_;
        RCLCPP_WARN(
          logger,
          "isolating chip %zu after %zu consecutive failures (%llu total); retaining its last "
          "valid samples and retrying after %zu frames",
          chip, consecutive_failures_[chip],
          static_cast<unsigned long long>(total_failures_[chip]), recovery_interval_frames_);
      } else {
        const bool realigned = prime_pipeline(chip);
        if (!transport_->ensure_all_chip_selects_high()) {
          RCLCPP_ERROR(logger, "cannot guarantee that every chip-select line is HIGH");
          active_ = false;
          return hardware_interface::return_type::ERROR;
        }
        if (!realigned) {
          chip_online_[chip] = false;
          recovery_countdown_[chip] = recovery_interval_frames_;
          RCLCPP_WARN(
            logger,
            "isolating chip %zu because its acquisition pipeline could not be realigned; "
            "retrying after %zu frames",
            chip, recovery_interval_frames_);
        }
      }
      continue;
    }
    consecutive_failures_[chip] = 0;
    sample_valid[chip] = true;
  }

  DoubleFrame next_raw_i = raw_i_state_;
  DoubleFrame next_raw_q = raw_q_state_;
  DoubleFrame next_value = value_state_;
  EmaFrame next_ema = ema_accumulator_;
  InitializedFrame next_initialized = ema_initialized_;
  RawFrame next_last_valid = last_valid_i_;

  for (std::size_t chip = 0; chip < active_devices_; ++chip) {
    if (!sample_valid[chip]) {
      continue;
    }
    for (std::size_t physical_channel = 0; physical_channel < kChannelsPerRaa;
      ++physical_channel)
    {
      const std::size_t logical_channel = kChannelMap[physical_channel];
      const uint16_t i_sample = staged_i[chip][physical_channel];
      const uint16_t q_sample = staged_q[chip][physical_channel];
      next_raw_i[chip][logical_channel] = static_cast<double>(i_sample);
      next_raw_q[chip][logical_channel] = static_cast<double>(q_sample);

      uint16_t filtered = next_last_valid[chip][logical_channel];
      if (i_sample > 0) {
        filtered = update_ema(
          i_sample, chip, logical_channel, next_ema, next_initialized);
        next_last_valid[chip][logical_channel] = filtered;
      }
      next_value[chip][logical_channel] = static_cast<double>(
        static_cast<int32_t>(filtered) -
        static_cast<int32_t>(tare_for_output(chip, logical_channel)));
    }
  }

  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now() - frame_start);
  record_timing(elapsed, period);

  raw_i_state_ = next_raw_i;
  raw_q_state_ = next_raw_q;
  value_state_ = next_value;
  ema_accumulator_ = next_ema;
  ema_initialized_ = next_initialized;
  last_valid_i_ = next_last_valid;
  return hardware_interface::return_type::OK;
}

uint16_t SscTactileHandHardwareInterface::update_ema(
  const uint16_t sample, const std::size_t chip, const std::size_t logical_channel,
  EmaFrame & ema, InitializedFrame & initialized) const noexcept
{
  return ema_filter_step(
    sample, ema_shift_, ema[chip][logical_channel], initialized[chip][logical_channel]);
}

uint16_t SscTactileHandHardwareInterface::tare_for_output(
  const std::size_t chip, const std::size_t logical_channel) const noexcept
{
  // Tare is stored under the selected sensor identity. After the pipeline is primed, that identity
  // is the same logical index used here, matching the reference firmware's channelMap behavior.
  return auto_tare_ ? tare_physical_[chip][logical_channel] : 0;
}

void SscTactileHandHardwareInterface::reset_runtime_state() noexcept
{
  reset_exported_state();
  ema_accumulator_ = {};
  ema_initialized_ = {};
  tare_physical_ = {};
  last_valid_i_ = {};
  chip_online_ = {};
  consecutive_failures_ = {};
  total_failures_ = {};
  recovery_countdown_ = {};
  reset_timing();
  last_error_log_time_ = {};
}

void SscTactileHandHardwareInterface::reset_exported_state() noexcept
{
  raw_i_state_ = {};
  raw_q_state_ = {};
  value_state_ = {};
}

void SscTactileHandHardwareInterface::reset_timing() noexcept
{
  timing_samples_ = 0;
  timing_total_ns_ = 0;
  timing_max_ns_ = 0;
  last_positive_period_ns_ = 0;
}

void SscTactileHandHardwareInterface::record_timing(
  const std::chrono::nanoseconds elapsed, const rclcpp::Duration & period)
{
  const int64_t observed_period_ns = period.nanoseconds();
  if (observed_period_ns > 0) {
    last_positive_period_ns_ = observed_period_ns;
  }
  timing_total_ns_ += elapsed.count();
  timing_max_ns_ = std::max(timing_max_ns_, elapsed.count());
  ++timing_samples_;
  if (timing_samples_ < kTimingSampleCount) {
    return;
  }

  const auto logger = rclcpp::get_logger(kLoggerName);
  const double average_ms =
    static_cast<double>(timing_total_ns_) / static_cast<double>(kTimingSampleCount) / 1.0e6;
  const double maximum_ms = static_cast<double>(timing_max_ns_) / 1.0e6;
  const double budget_ms = static_cast<double>(kTimingBudgetNs) / 1.0e6;
  if (last_positive_period_ns_ > 0) {
    const double observed_period_ms = static_cast<double>(last_positive_period_ns_) / 1.0e6;
    RCLCPP_INFO(
      logger, "full-frame timing over %zu cycles: average %.3f ms, maximum %.3f ms, "
      "nominal budget %.3f ms, last observed period %.3f ms",
      kTimingSampleCount, average_ms, maximum_ms, budget_ms, observed_period_ms);
  } else {
    RCLCPP_WARN(
      logger, "full-frame timing over %zu cycles: average %.3f ms, maximum %.3f ms; "
      "nominal budget %.3f ms; no positive controller period was observed",
      kTimingSampleCount, average_ms, maximum_ms, budget_ms);
  }
  if (timing_max_ns_ > kTimingBudgetNs) {
    RCLCPP_WARN(
      logger,
      "full-frame maximum %.3f ms exceeds nominal 40 Hz budget %.3f ms; acquisition remains "
      "active and monitoring continues",
      maximum_ms, budget_ms);
  }
  timing_samples_ = 0;
  timing_total_ns_ = 0;
  timing_max_ns_ = 0;
}

void SscTactileHandHardwareInterface::log_transport_error(
  const char * const operation, const bool throttle)
{
  const auto now = std::chrono::steady_clock::now();
  if (throttle && last_error_log_time_ != std::chrono::steady_clock::time_point{} &&
    now - last_error_log_time_ < std::chrono::seconds(1))
  {
    return;
  }
  last_error_log_time_ = now;

  const auto logger = rclcpp::get_logger(kLoggerName);
  const auto & error = transport_->last_error();
  const bool has_chip = error.chip < kRaaCount;
  const bool has_channel = error.channel < kChannelsPerRaa;
  if (has_chip && has_channel) {
    RCLCPP_ERROR(
      logger, "%s failed at chip %zu, physical channel %zu, CS offset %u: %s", operation,
      error.chip, error.channel, cs_line_offsets_[error.chip], error.message.c_str());
  } else if (has_chip) {
    RCLCPP_ERROR(
      logger, "%s failed at chip %zu, CS offset %u: %s", operation, error.chip,
      cs_line_offsets_[error.chip], error.message.c_str());
  } else {
    RCLCPP_ERROR(logger, "%s failed: %s", operation, error.message.c_str());
  }
}

}  // namespace ssc_tactile_hand_ros2_control

PLUGINLIB_EXPORT_CLASS(
  ssc_tactile_hand_ros2_control::SscTactileHandHardwareInterface,
  hardware_interface::SensorInterface)
