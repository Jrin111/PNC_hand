// SPDX-License-Identifier: Apache-2.0

// This target compiles the real hardware interface and substitutes only the SPI transport.
// It exercises exported ROS state handles without opening SPI/GPIO or publishing ROS commands.
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ssc_tactile_hand_ros2_control/ssc_tactile_hand_hardware_interface.hpp"

namespace tactile = ssc_tactile_hand_ros2_control;
using CallbackReturn = hardware_interface::CallbackReturn;
using ReturnType = hardware_interface::return_type;

namespace
{

struct FakeBus
{
  FakeBus()
  {
    initialize_ok.fill(true);
    measure_ok.fill(true);
    read_ok.fill(true);
    for (std::size_t chip = 0; chip < tactile::kRaaCount; ++chip) {
      for (std::size_t channel = 0; channel < tactile::kChannelsPerRaa; ++channel) {
        i[chip][channel] = 100 + chip * 10 + channel;
        q[chip][channel] = 200 + chip * 10 + channel;
      }
    }
  }

  bool allow_open{true};
  bool open{false};
  std::array<bool, tactile::kRaaCount> initialize_ok{};
  std::array<bool, tactile::kRaaCount> measure_ok{};
  std::array<bool, tactile::kRaaCount> read_ok{};
  std::array<std::size_t, tactile::kRaaCount> initialize_calls{};
  std::array<std::size_t, tactile::kRaaCount> read_calls{};
  tactile::RawFrame i{};
  tactile::RawFrame q{};
  std::size_t ensure_calls{0};
  std::size_t unsafe_on_ensure{0};
};

FakeBus bus;

void require(bool condition, const std::string & label)
{
  if (!condition) {
    throw std::runtime_error(label);
  }
}

struct Fixture
{
  explicit Fixture(
    std::size_t active_devices = tactile::kRaaCount, bool auto_tare = false,
    unsigned int ema_shift = 0, std::size_t failure_limit = 3)
  {
    bus = FakeBus{};
    hardware_interface::HardwareComponentInterfaceParams params;
    auto & info = params.hardware_info;
    info.name = "tactile_validity_test";
    info.type = "sensor";
    info.hardware_parameters = {
      {"spi_device", "/test/no-hardware"}, {"active_devices", std::to_string(active_devices)},
      {"auto_tare", auto_tare ? "true" : "false"}, {"ema_shift", std::to_string(ema_shift)},
      {"max_consecutive_failures", std::to_string(failure_limit)},
      {"recovery_interval_frames", "1"}};
    for (std::size_t chip = 0; chip < tactile::kRaaCount; ++chip) {
      for (std::size_t channel = 0; channel < tactile::kChannelsPerRaa; ++channel) {
        hardware_interface::ComponentInfo sensor;
        sensor.name = "raa" + std::to_string(chip) + "_ch" + std::to_string(channel);
        sensor.type = "sensor";
        for (const char * name : {"raw_i", "raw_q", "value"}) {
          hardware_interface::InterfaceInfo interface{};
          interface.name = name;
          sensor.state_interfaces.push_back(interface);
        }
        info.sensors.push_back(sensor);
      }
    }
    require(driver.on_init(params) == CallbackReturn::SUCCESS, "on_init succeeds");
    states = driver.export_state_interfaces();
    require(states.size() == 162, "all 162 existing state interfaces are exported");
  }

  double value(std::size_t chip, std::size_t channel, const std::string & interface) const
  {
    const std::string name =
      "raa" + std::to_string(chip) + "_ch" + std::to_string(channel) + "/" + interface;
    for (const auto & state : states) {
      if (state.get_name() == name) {
        const auto result = state.get_optional<double>();
        require(result.has_value(), "state handle is readable: " + name);
        return *result;
      }
    }
    throw std::runtime_error("missing state handle: " + name);
  }

  void expect_unknown(std::size_t chip) const
  {
    for (std::size_t channel = 0; channel < tactile::kChannelsPerRaa; ++channel) {
      for (const char * interface : {"raw_i", "raw_q", "value"}) {
        require(std::isnan(value(chip, channel, interface)),
          "unavailable chip " + std::to_string(chip) + " must export NaN for " + interface);
      }
    }
  }

  void expect_all_unknown() const
  {
    for (std::size_t chip = 0; chip < tactile::kRaaCount; ++chip) {
      expect_unknown(chip);
    }
  }

  void expect_unfiltered_sample(std::size_t chip) const
  {
    for (std::size_t physical = 0; physical < tactile::kChannelsPerRaa; ++physical) {
      const auto logical = tactile::kChannelMap[physical];
      require(value(chip, logical, "raw_i") == bus.i[chip][physical], "fresh raw I mapping");
      require(value(chip, logical, "raw_q") == bus.q[chip][physical], "fresh raw Q mapping");
      require(value(chip, logical, "value") == bus.i[chip][physical], "fresh processed value");
    }
  }

  void activate()
  {
    require(driver.on_activate(rclcpp_lifecycle::State{}) == CallbackReturn::SUCCESS,
      "activation succeeds");
  }

  ReturnType read()
  {
    return driver.read(rclcpp::Time(0), rclcpp::Duration::from_seconds(0.025));
  }

  tactile::SscTactileHandHardwareInterface driver;
  std::vector<hardware_interface::StateInterface> states;
};

void lifecycle_and_disabled_chips()
{
  Fixture fixture(2);
  fixture.expect_all_unknown();
  require(fixture.read() == ReturnType::ERROR, "read before activation is rejected");
  fixture.expect_all_unknown();
  fixture.activate();
  fixture.expect_all_unknown();
  require(fixture.read() == ReturnType::OK, "healthy read succeeds");
  fixture.expect_unfiltered_sample(0);
  fixture.expect_unfiltered_sample(1);
  for (std::size_t chip = 2; chip < tactile::kRaaCount; ++chip) {
    fixture.expect_unknown(chip);
    require(bus.read_calls[chip] == 0, "disabled chips are never sampled");
  }
  require(fixture.driver.on_deactivate(rclcpp_lifecycle::State{}) == CallbackReturn::SUCCESS,
    "deactivation succeeds");
  fixture.expect_all_unknown();
  require(fixture.read() == ReturnType::ERROR, "read after deactivation is rejected");
  fixture.expect_all_unknown();
  fixture.activate();
  fixture.expect_all_unknown();
}

void partial_failure_isolation_and_recovery()
{
  Fixture fixture;
  fixture.activate();
  require(fixture.read() == ReturnType::OK, "healthy baseline read");
  for (std::size_t chip = 0; chip < tactile::kRaaCount; ++chip) {
    fixture.expect_unfiltered_sample(chip);
  }
  constexpr std::size_t failed_chip = 4;
  bus.read_ok[failed_chip] = false;
  const auto check_failure_frame = [&fixture]() {
      for (auto & chip : bus.i) {
        for (auto & channel : chip) {
          ++channel;
        }
      }
      require(fixture.read() == ReturnType::OK, "one unavailable chip does not stop acquisition");
      fixture.expect_unknown(failed_chip);
      for (std::size_t chip = 0; chip < tactile::kRaaCount; ++chip) {
        if (chip != failed_chip) {
          fixture.expect_unfiltered_sample(chip);
        }
      }
    };
  check_failure_frame();  // The very first failed read must invalidate all 18 chip states.
  check_failure_frame();
  check_failure_frame();  // Threshold reached: chip is now isolated.
  const auto isolated_read_count = bus.read_calls[failed_chip];
  check_failure_frame();  // Recovery countdown, not a new sample.
  require(bus.read_calls[failed_chip] == isolated_read_count, "isolated chip is not sampled");
  bus.initialize_ok[failed_chip] = false;
  const auto initialize_count = bus.initialize_calls[failed_chip];
  check_failure_frame();
  require(bus.initialize_calls[failed_chip] == initialize_count + 1, "recovery init attempted");
  require(bus.read_calls[failed_chip] == isolated_read_count, "failed recovery cannot publish");
  check_failure_frame();  // Recovery countdown after failed initialization.
  bus.initialize_ok[failed_chip] = true;
  bus.measure_ok[failed_chip] = false;
  check_failure_frame();  // Initialization succeeds but pipeline priming fails.
  require(bus.read_calls[failed_chip] == isolated_read_count, "failed prime cannot publish");
  check_failure_frame();
  bus.measure_ok[failed_chip] = true;
  check_failure_frame();  // Recovered transport still cannot supply a complete chip sample.
  require(bus.read_calls[failed_chip] == isolated_read_count + 1, "recovered chip read attempted");
  bus.read_ok[failed_chip] = true;
  require(fixture.read() == ReturnType::OK, "fresh sample restores recovered chip");
  for (std::size_t chip = 0; chip < tactile::kRaaCount; ++chip) {
    fixture.expect_unfiltered_sample(chip);
  }
}

void filter_and_tare_survive_transient_failure()
{
  Fixture fixture(2, true, 2);
  for (auto & chip : bus.i) {
    chip.fill(100);
  }
  fixture.activate();  // Real tare code averages the fake bus samples to 100.
  bus.i[0].fill(108);
  require(fixture.read() == ReturnType::OK, "filtered initial read");
  require(fixture.value(0, 0, "value") == 2, "tare 100 and EMA yield 102 - 100");
  bus.read_ok[0] = false;
  bus.i[0].fill(1000);  // Deliberate partial-read data must not enter the filter.
  require(fixture.read() == ReturnType::OK, "filtered failed read remains chip-local");
  fixture.expect_unknown(0);
  bus.read_ok[0] = true;
  bus.i[0].fill(108);
  require(fixture.read() == ReturnType::OK, "filtered transient recovery");
  require(fixture.value(0, 0, "raw_i") == 108, "fresh recovered raw data");
  require(fixture.value(0, 0, "value") == 3, "integer EMA and tare survive the NaN output frame");
  bus.i[0].fill(0);
  require(fixture.read() == ReturnType::OK, "complete zero-I sample is still valid transport data");
  require(fixture.value(0, 0, "raw_i") == 0, "zero raw I is preserved");
  require(fixture.value(0, 0, "value") == 3, "existing zero-I filtered hold is unchanged");
}

void global_faults_invalidate_every_chip()
{
  for (unsigned int fault = 0; fault < 4; ++fault) {
    Fixture fixture(2, false, 0, fault == 3 ? 1 : 3);
    fixture.activate();
    require(fixture.read() == ReturnType::OK, "healthy frame before global fault");
    if (fault == 0) {
      bus.open = false;
    } else if (fault == 3) {
      bus.read_ok[0] = false;
      require(fixture.read() == ReturnType::OK, "isolate chip before recovery-CS fault");
      require(fixture.read() == ReturnType::OK, "wait before recovery-CS fault");
      bus.read_ok[0] = true;
      bus.unsafe_on_ensure = bus.ensure_calls + 1;
    } else {
      bus.read_ok[0] = false;
      // First guard follows failed read; second guard follows pipeline realignment.
      bus.unsafe_on_ensure = bus.ensure_calls + fault;
    }
    require(fixture.read() == ReturnType::ERROR, "global transport/CS fault returns ERROR");
    fixture.expect_all_unknown();
    require(fixture.read() == ReturnType::ERROR, "unavailable system cannot publish a later frame");
    fixture.expect_all_unknown();
  }
}

void activation_failures_never_export_stale_data()
{
  Fixture fixture(2);
  fixture.activate();
  require(fixture.read() == ReturnType::OK, "healthy frame before reactivation failure");
  bus.allow_open = false;
  require(fixture.driver.on_activate(rclcpp_lifecycle::State{}) == CallbackReturn::ERROR,
    "failed transport open rejects activation");
  fixture.expect_all_unknown();
  bus.allow_open = true;
  bus.initialize_ok.fill(false);
  require(fixture.driver.on_activate(rclcpp_lifecycle::State{}) == CallbackReturn::ERROR,
    "all chips unavailable rejects activation");
  fixture.expect_all_unknown();
  bus.initialize_ok[0] = true;
  fixture.activate();
  fixture.expect_all_unknown();
  require(fixture.read() == ReturnType::OK, "partially available activation can sample healthy chip");
  fixture.expect_unfiltered_sample(0);
  fixture.expect_unknown(1);
}

}  // namespace

// Test-only link substitution. Production targets continue to use src/spi_transport.cpp.
namespace ssc_tactile_hand_ros2_control
{

SpiTransport::SpiTransport(
  std::string spi_device, uint32_t speed_hz, uint8_t mode, std::string gpio_chip,
  std::array<unsigned int, kRaaCount> cs_line_offsets, std::size_t active_devices,
  std::chrono::microseconds measurement_wait, ResponseCheckMode response_check_mode)
: spi_device_(std::move(spi_device)), speed_hz_(speed_hz), mode_(mode),
  gpio_chip_path_(std::move(gpio_chip)), cs_line_offsets_(cs_line_offsets),
  active_devices_(active_devices), measurement_wait_(measurement_wait),
  response_check_mode_(response_check_mode)
{
}

SpiTransport::~SpiTransport() {close();}
bool SpiTransport::open() {bus.open = bus.allow_open; return bus.open;}
void SpiTransport::close() noexcept {bus.open = false;}
bool SpiTransport::is_open() const noexcept {return bus.open;}

bool SpiTransport::initialize_chip(std::size_t chip)
{
  ++bus.initialize_calls[chip];
  last_error_ = {chip, kInvalidIndex, "injected initialize failure"};
  return bus.initialize_ok[chip];
}

bool SpiTransport::measure_one_channel(
  std::size_t chip, std::size_t physical_channel, uint16_t & i_value, uint16_t & q_value)
{
  i_value = bus.i[chip][physical_channel];
  q_value = bus.q[chip][physical_channel];
  last_error_ = {chip, physical_channel, "injected measurement failure"};
  return bus.measure_ok[chip];
}

bool SpiTransport::read_chip_channels(
  std::size_t chip, ChipSamples & i_values, ChipSamples & q_values)
{
  ++bus.read_calls[chip];
  // Even a failed read can have written samples before failing on a later channel.
  i_values = bus.i[chip];
  q_values = bus.q[chip];
  last_error_ = {chip, 3, "injected incomplete chip sample"};
  return bus.read_ok[chip];
}

bool SpiTransport::ensure_all_chip_selects_high() noexcept
{
  ++bus.ensure_calls;
  return bus.ensure_calls != bus.unsafe_on_ensure;
}

const SpiTransport::ErrorContext & SpiTransport::last_error() const noexcept {return last_error_;}

}  // namespace ssc_tactile_hand_ros2_control

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  const std::vector<std::pair<const char *, void (*)()>> tests{
    {"lifecycle and disabled chips", lifecycle_and_disabled_chips},
    {"first failure, isolation and recovery", partial_failure_isolation_and_recovery},
    {"filter and tare preservation", filter_and_tare_survive_transient_failure},
    {"global faults invalidate all chips", global_faults_invalidate_every_chip},
    {"activation failure validity", activation_failures_never_export_stale_data}};
  bool passed = true;
  for (const auto & test : tests) {
    try {
      test.second();
      std::cout << "PASS: " << test.first << '\n';
    } catch (const std::exception & error) {
      passed = false;
      std::cerr << "FAIL: " << test.first << ": " << error.what() << '\n';
    }
  }
  rclcpp::shutdown();
  return passed ? 0 : 1;
}
