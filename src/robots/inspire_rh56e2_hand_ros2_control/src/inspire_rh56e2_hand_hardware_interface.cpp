// ********************************************************************************************************************
// Copyright [2026] Renesas Electronics Corporation and/or its licensors. All Rights Reserved.
//
// The contents of this file (the "contents") are proprietary and confidential to Renesas Electronics Corporation
// and/or its licensors ("Renesas") and subject to statutory and contractual protections.
//
// Unless otherwise expressly agreed in writing between Renesas and you: 1) you may not use, copy, modify, distribute,
// display, or perform the contents; 2) you may not use any name or mark of Renesas for advertising or publicity
// purposes or in connection with your use of the contents; 3) RENESAS MAKES NO WARRANTY OR REPRESENTATIONS ABOUT THE
// SUITABILITY OF THE CONTENTS FOR ANY PURPOSE; THE CONTENTS ARE PROVIDED "AS IS" WITHOUT ANY EXPRESS OR IMPLIED
// WARRANTY, INCLUDING THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND
// NON-INFRINGEMENT; AND 4) RENESAS SHALL NOT BE LIABLE FOR ANY DIRECT, INDIRECT, SPECIAL, OR CONSEQUENTIAL DAMAGES,
// INCLUDING DAMAGES RESULTING FROM LOSS OF USE, DATA, OR PROJECTS, WHETHER IN AN ACTION OF CONTRACT OR TORT, ARISING
// OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THE CONTENTS. Third-party contents included in this file may
// be subject to different terms.
// ********************************************************************************************************************

#include "inspire_rh56e2_hand_ros2_control/inspire_rh56e2_hand_hardware_interface.hpp"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "hardware_interface/types/hardware_interface_type_values.hpp"
#include "rclcpp/rclcpp.hpp"

namespace inspire_rh56e2_hand_ros2_control
{

hardware_interface::CallbackReturn InspireRH56E2HandHardwareInterface::on_init(
  const hardware_interface::HardwareComponentInterfaceParams & params)
{
  if (hardware_interface::SystemInterface::on_init(params) != CallbackReturn::SUCCESS) {
    return CallbackReturn::ERROR;
  }

  // Initialize parameters
  serial_port_ = info_.hardware_parameters["serial_port"];
  baudrate_ = std::stoi(info_.hardware_parameters.at("baudrate"));
  hand_id_ = 1;  // Default hand ID

  // Parse optional hand_speed parameter (0-1000, default 1000 = max speed)
  auto it = info_.hardware_parameters.find("hand_speed");
  hand_speed_ = (it != info_.hardware_parameters.end()) ? std::stoi(it->second) : 1000;
  hand_speed_ = std::max(0, std::min(1000, hand_speed_));

  RCLCPP_INFO(
    rclcpp::get_logger("InspireRH56E2HandHardwareInterface"),
    "Serial port: %s, Baudrate: %d, Hand ID: %d, Hand speed: %d", serial_port_.c_str(), baudrate_,
    hand_id_, hand_speed_);

  // Initialize joint vectors
  hw_commands_.resize(info_.joints.size(), 0.0);
  hw_positions_.resize(info_.joints.size(), 0.0);
  hw_velocities_.resize(info_.joints.size(), 0.0);
  hw_force_commands_.resize(info_.joints.size(), 1000.0);  // Default force threshold: 1000g
  hw_forces_.resize(info_.joints.size(), 0.0);
  // Default motion mode 0 (Speed/Force Protection) - safe default for every joint
  hw_mode_commands_.resize(info_.joints.size(), 0.0);

  // Initialize state tracking
  first_read_completed_ = false;

  // Initialize joint limits from URDF/XACRO definitions
  joint_min_limits_.resize(info_.joints.size());
  joint_max_limits_.resize(info_.joints.size());

  for (size_t i = 0; i < info_.joints.size(); ++i) {
    // Extract limits from URDF joint info
    const auto & joint = info_.joints[i];
    joint_min_limits_[i] = std::stod(joint.command_interfaces[0].min);
    joint_max_limits_[i] = std::stod(joint.command_interfaces[0].max);

    RCLCPP_INFO(
      rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Joint %s: min=%.3f, max=%.3f",
      joint.name.c_str(), joint_min_limits_[i], joint_max_limits_[i]);
  }

  // Initialize last_written_angles_ to invalid value to force first write
  last_written_angles_.fill(-1);
  last_written_forces_.fill(-1);

  return CallbackReturn::SUCCESS;
}

std::vector<hardware_interface::StateInterface>
InspireRH56E2HandHardwareInterface::export_state_interfaces()
{
  std::vector<hardware_interface::StateInterface> state_interfaces;

  for (size_t i = 0; i < info_.joints.size(); i++) {
    state_interfaces.emplace_back(
      hardware_interface::StateInterface(
        info_.joints[i].name, hardware_interface::HW_IF_POSITION, &hw_positions_[i]));
    state_interfaces.emplace_back(
      hardware_interface::StateInterface(
        info_.joints[i].name, hardware_interface::HW_IF_VELOCITY, &hw_velocities_[i]));
    state_interfaces.emplace_back(
      hardware_interface::StateInterface(
        info_.joints[i].name, hardware_interface::HW_IF_FORCE, &hw_forces_[i]));
  }

  return state_interfaces;
}

std::vector<hardware_interface::CommandInterface>
InspireRH56E2HandHardwareInterface::export_command_interfaces()
{
  std::vector<hardware_interface::CommandInterface> command_interfaces;

  for (size_t i = 0; i < info_.joints.size(); i++) {
    command_interfaces.emplace_back(
      hardware_interface::CommandInterface(
        info_.joints[i].name, hardware_interface::HW_IF_POSITION, &hw_commands_[i]));
    command_interfaces.emplace_back(
      hardware_interface::CommandInterface(
        info_.joints[i].name, "force_threshold", &hw_force_commands_[i]));
    command_interfaces.emplace_back(
      hardware_interface::CommandInterface(
        info_.joints[i].name, "motion_mode", &hw_mode_commands_[i]));
  }

  return command_interfaces;
}

hardware_interface::CallbackReturn InspireRH56E2HandHardwareInterface::on_activate(
  const rclcpp_lifecycle::State & /*previous_state*/)
{
  RCLCPP_INFO(rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Activating...");

  if (!open_serial_port()) {
    RCLCPP_ERROR(
      rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Failed to open serial port %s",
      serial_port_.c_str());
    return CallbackReturn::ERROR;
  }

  // Build the reusable motion mode write frame template (registers 1625-1630, 6x byte) and
  // perform a one-time write of mode 0 (Speed/Force Protection) for every joint before any
  // motion commands. The mode is configurable at runtime via the "motion_mode" command
  // interface; write() re-sends this frame whenever a commanded mode changes.
  // Modes: 0 = stops at target angle or force limit, 1 = closed-loop force control, 2 = hold.
  {
    mode_write_frame_[0] = 0xEB;
    mode_write_frame_[1] = 0x90;
    mode_write_frame_[2] = hand_id_;
    mode_write_frame_[3] = static_cast<uint8_t>(NUM_JOINTS + 3);  // length
    mode_write_frame_[4] = 0x12;                                  // write command
    mode_write_frame_[5] = static_cast<uint8_t>(MOTION_MODE_ADDR & 0xFF);
    mode_write_frame_[6] = static_cast<uint8_t>((MOTION_MODE_ADDR >> 8) & 0xFF);
    for (size_t i = 0; i < NUM_JOINTS; ++i) {
      mode_write_frame_[7 + i] = 0x00;  // 6 joints = mode 0
    }
    uint32_t mode_sum = 0;
    for (size_t i = 2; i < MODE_WRITE_FRAME_SIZE - 1; ++i) {
      mode_sum += mode_write_frame_[i];
    }
    mode_write_frame_[MODE_WRITE_FRAME_SIZE - 1] = static_cast<uint8_t>(mode_sum & 0xFF);

    if (
      ::write(serial_fd_, mode_write_frame_.data(), MODE_WRITE_FRAME_SIZE) !=
      static_cast<ssize_t>(MODE_WRITE_FRAME_SIZE)) {
      RCLCPP_ERROR(
        rclcpp::get_logger("InspireRH56E2HandHardwareInterface"),
        "Failed to send motion mode frame");
      close_serial_port();
      return CallbackReturn::ERROR;
    }

    // Read ACK (9 bytes)
    if (!read_exact(mode_write_ack_buf_.data(), MODE_WRITE_ACK_SIZE)) {
      RCLCPP_ERROR(
        rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Failed to read motion mode ACK");
      close_serial_port();
      return CallbackReturn::ERROR;
    }

    if (
      mode_write_ack_buf_[0] != 0x90 || mode_write_ack_buf_[1] != 0xEB ||
      mode_write_ack_buf_[2] != hand_id_ || mode_write_ack_buf_[4] != 0x12) {
      RCLCPP_ERROR(
        rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Invalid motion mode ACK");
      close_serial_port();
      return CallbackReturn::ERROR;
    }

    // Hardware now holds mode 0 for all joints; remember it for change detection in write().
    last_written_modes_.fill(0);

    RCLCPP_INFO(
      rclcpp::get_logger("InspireRH56E2HandHardwareInterface"),
      "Set all joints to Speed/Force Protection mode (mode 0)");
  }

  // Set target speed for all joints (one-time configuration).
  // Register 1522-1533: 6x short, range 0-1000 (1000 = approx 600ms full travel).
  {
    constexpr size_t SPEED_DATA_LEN = TOTAL_ANGLE_BYTES;         // 12 bytes (6x int16)
    constexpr size_t SPEED_FRAME_SIZE = 7 + SPEED_DATA_LEN + 1;  // header(7) + data + checksum
    std::array<uint8_t, SPEED_FRAME_SIZE> speed_frame{};
    speed_frame[0] = 0xEB;
    speed_frame[1] = 0x90;
    speed_frame[2] = hand_id_;
    speed_frame[3] = static_cast<uint8_t>(SPEED_DATA_LEN + 3);
    speed_frame[4] = 0x12;  // write command
    speed_frame[5] = static_cast<uint8_t>(SPEED_SET_ADDR & 0xFF);
    speed_frame[6] = static_cast<uint8_t>((SPEED_SET_ADDR >> 8) & 0xFF);
    int16_t spd = static_cast<int16_t>(hand_speed_);
    for (size_t i = 0; i < NUM_JOINTS; ++i) {
      speed_frame[7 + i * 2] = static_cast<uint8_t>(spd & 0xFF);
      speed_frame[7 + i * 2 + 1] = static_cast<uint8_t>((spd >> 8) & 0xFF);
    }
    uint32_t spd_sum = 0;
    for (size_t i = 2; i < SPEED_FRAME_SIZE - 1; ++i) {
      spd_sum += speed_frame[i];
    }
    speed_frame[SPEED_FRAME_SIZE - 1] = static_cast<uint8_t>(spd_sum & 0xFF);

    if (
      ::write(serial_fd_, speed_frame.data(), SPEED_FRAME_SIZE) !=
      static_cast<ssize_t>(SPEED_FRAME_SIZE)) {
      RCLCPP_ERROR(
        rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Failed to send speed set frame");
      close_serial_port();
      return CallbackReturn::ERROR;
    }

    std::array<uint8_t, 9> speed_ack{};
    if (!read_exact(speed_ack.data(), speed_ack.size())) {
      RCLCPP_ERROR(
        rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Failed to read speed set ACK");
      close_serial_port();
      return CallbackReturn::ERROR;
    }

    if (
      speed_ack[0] != 0x90 || speed_ack[1] != 0xEB || speed_ack[2] != hand_id_ ||
      speed_ack[4] != 0x12) {
      RCLCPP_ERROR(
        rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Invalid speed set ACK");
      close_serial_port();
      return CallbackReturn::ERROR;
    }

    RCLCPP_INFO(
      rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Set all joints target speed to %d",
      hand_speed_);
  }

  // Pre-build the ANGLE_ACT read request frame (constant across all cycles)
  angle_read_frame_[0] = 0xEB;
  angle_read_frame_[1] = 0x90;
  angle_read_frame_[2] = hand_id_;
  angle_read_frame_[3] = 0x04;
  angle_read_frame_[4] = 0x11;
  angle_read_frame_[5] = static_cast<uint8_t>(ANGLE_ACT_ADDR & 0xFF);
  angle_read_frame_[6] = static_cast<uint8_t>((ANGLE_ACT_ADDR >> 8) & 0xFF);
  angle_read_frame_[7] = TOTAL_ANGLE_BYTES;
  uint32_t rd_sum = 0;
  for (size_t i = 2; i < ANGLE_READ_FRAME_SIZE - 1; ++i) {
    rd_sum += angle_read_frame_[i];
  }
  angle_read_frame_[ANGLE_READ_FRAME_SIZE - 1] = static_cast<uint8_t>(rd_sum & 0xFF);

  // Pre-build the ANGLE_SET write frame template (data + checksum updated each cycle)
  angle_write_frame_[0] = 0xEB;
  angle_write_frame_[1] = 0x90;
  angle_write_frame_[2] = hand_id_;
  angle_write_frame_[3] = static_cast<uint8_t>(TOTAL_ANGLE_BYTES + 3);
  angle_write_frame_[4] = 0x12;
  angle_write_frame_[5] = static_cast<uint8_t>(ANGLE_SET_ADDR & 0xFF);
  angle_write_frame_[6] = static_cast<uint8_t>((ANGLE_SET_ADDR >> 8) & 0xFF);

  // Reset the hand to the fully-open pose on initialization so it always starts from a known,
  // safe configuration. Motion mode 0 and the target speed were configured above, so the hand
  // physically travels to this target.
  {
    RCLCPP_INFO(
      rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Resetting hand to open pose");

    // Read the 6 raw ANGLE_ACT counts (0-1000) into `out`; returns false on a transient serial
    // hiccup so the caller can retry. Used by the settle loop below.
    auto read_angle_act = [this](std::array<int16_t, NUM_JOINTS> & out) -> bool {
      if (
        ::write(serial_fd_, angle_read_frame_.data(), ANGLE_READ_FRAME_SIZE) !=
          static_cast<ssize_t>(ANGLE_READ_FRAME_SIZE) ||
        !read_exact(angle_read_resp_buf_.data(), ANGLE_READ_RESP_SIZE) ||
        angle_read_resp_buf_[0] != 0x90 || angle_read_resp_buf_[1] != 0xEB ||
        angle_read_resp_buf_[2] != hand_id_ || angle_read_resp_buf_[4] != 0x11) {
        return false;
      }
      for (size_t i = 0; i < NUM_JOINTS; ++i) {
        out[i] = static_cast<int16_t>(
          angle_read_resp_buf_[7 + i * 2] | (angle_read_resp_buf_[7 + i * 2 + 1] << 8));
      }
      return true;
    };

    constexpr int16_t OPEN_ANGLE_VALUE = 1000;  // fully open
    for (size_t i = 0; i < NUM_JOINTS; ++i) {
      angle_write_frame_[7 + i * 2] = static_cast<uint8_t>(OPEN_ANGLE_VALUE & 0xFF);
      angle_write_frame_[7 + i * 2 + 1] = static_cast<uint8_t>((OPEN_ANGLE_VALUE >> 8) & 0xFF);
    }
    uint32_t open_sum = 0;
    for (size_t i = 2; i < ANGLE_WRITE_FRAME_SIZE - 1; ++i) {
      open_sum += angle_write_frame_[i];
    }
    angle_write_frame_[ANGLE_WRITE_FRAME_SIZE - 1] = static_cast<uint8_t>(open_sum & 0xFF);

    if (
      ::write(serial_fd_, angle_write_frame_.data(), ANGLE_WRITE_FRAME_SIZE) !=
      static_cast<ssize_t>(ANGLE_WRITE_FRAME_SIZE)) {
      RCLCPP_ERROR(
        rclcpp::get_logger("InspireRH56E2HandHardwareInterface"),
        "Failed to send open-pose reset frame");
      close_serial_port();
      return CallbackReturn::ERROR;
    }

    if (!read_exact(angle_write_ack_buf_.data(), ANGLE_WRITE_ACK_SIZE)) {
      RCLCPP_ERROR(
        rclcpp::get_logger("InspireRH56E2HandHardwareInterface"),
        "Failed to read open-pose reset ACK");
      close_serial_port();
      return CallbackReturn::ERROR;
    }

    if (
      angle_write_ack_buf_[0] != 0x90 || angle_write_ack_buf_[1] != 0xEB ||
      angle_write_ack_buf_[2] != hand_id_ || angle_write_ack_buf_[4] != 0x12) {
      RCLCPP_ERROR(
        rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Invalid open-pose reset ACK");
      close_serial_port();
      return CallbackReturn::ERROR;
    }

    // The ACK only confirms the command was accepted, not that the fingers have finished
    // travelling (full travel is ~600 ms). Block until the joints stop moving so the motion
    // completes before on_activate returns; otherwise the control loop's first read() would latch
    // hw_commands_ to a mid-travel position and freeze the hand partway open. Stop is detected from
    // per-joint ANGLE_ACT deltas (robust to each joint's open limit reading differently), and only
    // after MIN_SETTLE_READS so a joint that is slow to start moving is not mistaken for stopped.
    {
      constexpr int MAX_SETTLE_POLLS = 80;  // ~1.6 s ceiling at 20 ms/poll
      constexpr int MIN_SETTLE_READS = 10;  // ~200 ms command-latency guard
      constexpr useconds_t SETTLE_POLL_INTERVAL_US = 20000;
      constexpr int SETTLE_DELTA = 5;  // ANGLE_ACT counts treated as "stopped"
      std::array<int16_t, NUM_JOINTS> prev_act{};
      std::array<int16_t, NUM_JOINTS> act{};
      int reads_done = 0;
      bool settled = false;
      for (int poll = 0; poll < MAX_SETTLE_POLLS && !settled; ++poll) {
        usleep(SETTLE_POLL_INTERVAL_US);

        if (!read_angle_act(act)) {
          continue;  // transient read hiccup - retry on next poll
        }
        ++reads_done;

        // Only trust "stopped" once enough time has elapsed for motion to actually begin.
        if (reads_done >= MIN_SETTLE_READS) {
          settled = true;
          for (size_t i = 0; i < NUM_JOINTS; ++i) {
            int delta = static_cast<int>(act[i]) - static_cast<int>(prev_act[i]);
            if (delta > SETTLE_DELTA || delta < -SETTLE_DELTA) {
              settled = false;
              break;
            }
          }
        }
        prev_act = act;
      }

      if (!settled) {
        RCLCPP_WARN(
          rclcpp::get_logger("InspireRH56E2HandHardwareInterface"),
          "Open-pose reset motion did not settle within timeout; continuing anyway");
      }
    }

    RCLCPP_INFO(
      rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Reset hand to open pose");
  }

  // Pre-build the FORCE_ACT read request frame (constant across all cycles)
  force_read_frame_[0] = 0xEB;
  force_read_frame_[1] = 0x90;
  force_read_frame_[2] = hand_id_;
  force_read_frame_[3] = 0x04;
  force_read_frame_[4] = 0x11;
  force_read_frame_[5] = static_cast<uint8_t>(FORCE_ACT_ADDR & 0xFF);
  force_read_frame_[6] = static_cast<uint8_t>((FORCE_ACT_ADDR >> 8) & 0xFF);
  force_read_frame_[7] = TOTAL_FORCE_BYTES;
  uint32_t frd_sum = 0;
  for (size_t i = 2; i < FORCE_READ_FRAME_SIZE - 1; ++i) {
    frd_sum += force_read_frame_[i];
  }
  force_read_frame_[FORCE_READ_FRAME_SIZE - 1] = static_cast<uint8_t>(frd_sum & 0xFF);

  // Pre-build the FORCE_SET write frame template (data + checksum updated each cycle)
  force_write_frame_[0] = 0xEB;
  force_write_frame_[1] = 0x90;
  force_write_frame_[2] = hand_id_;
  force_write_frame_[3] = static_cast<uint8_t>(TOTAL_FORCE_BYTES + 3);
  force_write_frame_[4] = 0x12;
  force_write_frame_[5] = static_cast<uint8_t>(FORCE_SET_ADDR & 0xFF);
  force_write_frame_[6] = static_cast<uint8_t>((FORCE_SET_ADDR >> 8) & 0xFF);

  // Force first write by invalidating last written values
  last_written_angles_.fill(-1);
  last_written_forces_.fill(-1);

  // Reset state tracking - let first read() call initialize positions
  first_read_completed_ = false;

  RCLCPP_INFO(rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Successfully activated");
  return CallbackReturn::SUCCESS;
}

hardware_interface::CallbackReturn InspireRH56E2HandHardwareInterface::on_deactivate(
  const rclcpp_lifecycle::State & /*previous_state*/)
{
  RCLCPP_INFO(rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Deactivating...");
  close_serial_port();
  return CallbackReturn::SUCCESS;
}

hardware_interface::return_type InspireRH56E2HandHardwareInterface::read(
  const rclcpp::Time & /*time*/, const rclcpp::Duration & period)
{
  // Send pre-built ANGLE_ACT read frame (no per-cycle frame construction)
  if (
    ::write(serial_fd_, angle_read_frame_.data(), ANGLE_READ_FRAME_SIZE) !=
    static_cast<ssize_t>(ANGLE_READ_FRAME_SIZE)) {
    RCLCPP_WARN(
      rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Failed to send angle read frame");
    return hardware_interface::return_type::ERROR;
  }

  // Read full response in one robust call (no two-phase header + data reads)
  if (!read_exact(angle_read_resp_buf_.data(), ANGLE_READ_RESP_SIZE)) {
    RCLCPP_WARN(
      rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Failed to read angle response");
    return hardware_interface::return_type::ERROR;
  }

  // Verify response header
  if (
    angle_read_resp_buf_[0] != 0x90 || angle_read_resp_buf_[1] != 0xEB ||
    angle_read_resp_buf_[2] != hand_id_ || angle_read_resp_buf_[4] != 0x11) {
    RCLCPP_WARN(
      rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Invalid angle response header");
    return hardware_interface::return_type::ERROR;
  }

  // Parse angles directly from response (data starts at offset 7, pre-allocated buffer)
  for (size_t i = 0; i < NUM_JOINTS; ++i) {
    angle_read_buf_[i] = static_cast<int16_t>(
      angle_read_resp_buf_[7 + i * 2] | (angle_read_resp_buf_[7 + i * 2 + 1] << 8));
  }

  // Store previous positions using pre-allocated buffer (no heap allocation)
  std::copy(hw_positions_.begin(), hw_positions_.end(), prev_positions_.begin());

  // Convert hardware values to joint positions
  for (size_t urdf_idx = 0; urdf_idx < NUM_JOINTS; ++urdf_idx) {
    size_t hw_idx = urdf_to_hw_index(urdf_idx);
    hw_positions_[urdf_idx] = hardware_value_to_position(angle_read_buf_[hw_idx], urdf_idx);
  }

  // Calculate joint velocities using finite difference
  if (first_read_completed_ && period.seconds() > 0.0) {
    for (size_t i = 0; i < NUM_JOINTS; ++i) {
      hw_velocities_[i] = (hw_positions_[i] - prev_positions_[i]) / period.seconds();
    }
  } else {
    std::fill(hw_velocities_.begin(), hw_velocities_.end(), 0.0);
  }

  // Initialize commands to current positions on first read
  if (!first_read_completed_) {
    std::copy(hw_positions_.begin(), hw_positions_.end(), hw_commands_.begin());
    first_read_completed_ = true;
    RCLCPP_INFO(
      rclcpp::get_logger("InspireRH56E2HandHardwareInterface"),
      "First read completed - initialized positions and commands");
  }

  // Read FORCE_ACT registers
  if (
    ::write(serial_fd_, force_read_frame_.data(), FORCE_READ_FRAME_SIZE) !=
    static_cast<ssize_t>(FORCE_READ_FRAME_SIZE)) {
    RCLCPP_WARN(
      rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Failed to send force read frame");
    return hardware_interface::return_type::ERROR;
  }

  if (!read_exact(force_read_resp_buf_.data(), FORCE_READ_RESP_SIZE)) {
    RCLCPP_WARN(
      rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Failed to read force response");
    return hardware_interface::return_type::ERROR;
  }

  if (
    force_read_resp_buf_[0] != 0x90 || force_read_resp_buf_[1] != 0xEB ||
    force_read_resp_buf_[2] != hand_id_ || force_read_resp_buf_[4] != 0x11) {
    RCLCPP_WARN(
      rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Invalid force response header");
    return hardware_interface::return_type::ERROR;
  }

  for (size_t i = 0; i < NUM_JOINTS; ++i) {
    force_read_buf_[i] = static_cast<int16_t>(
      force_read_resp_buf_[7 + i * 2] | (force_read_resp_buf_[7 + i * 2 + 1] << 8));
  }

  // Map hardware force values to URDF joint order (store as grams)
  for (size_t urdf_idx = 0; urdf_idx < NUM_JOINTS; ++urdf_idx) {
    size_t hw_idx = urdf_to_hw_index(urdf_idx);
    hw_forces_[urdf_idx] = static_cast<double>(force_read_buf_[hw_idx]);
  }

  return hardware_interface::return_type::OK;
}

hardware_interface::return_type InspireRH56E2HandHardwareInterface::write(
  const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/)
{
  // Write Motion Mode registers (1625-1630, 1 byte per joint).
  // Clamp each commanded mode to a valid value and map URDF joint order to hardware order.
  for (size_t urdf_idx = 0; urdf_idx < NUM_JOINTS; ++urdf_idx) {
    size_t hw_idx = urdf_to_hw_index(urdf_idx);
    double rounded = std::round(hw_mode_commands_[urdf_idx]);
    double clamped = std::max(0.0, std::min(static_cast<double>(MAX_MOTION_MODE), rounded));
    mode_write_buf_[hw_idx] = static_cast<uint8_t>(clamped);
  }

  // Skip serial I/O if motion modes haven't changed
  if (mode_write_buf_ != last_written_modes_) {
    // Update data portion of pre-built mode write frame (1 byte per joint)
    for (size_t i = 0; i < NUM_JOINTS; ++i) {
      mode_write_frame_[7 + i] = mode_write_buf_[i];
    }

    // Recalculate checksum
    uint32_t msum = 0;
    for (size_t i = 2; i < MODE_WRITE_FRAME_SIZE - 1; ++i) {
      msum += mode_write_frame_[i];
    }
    mode_write_frame_[MODE_WRITE_FRAME_SIZE - 1] = static_cast<uint8_t>(msum & 0xFF);

    // Send mode write frame
    if (
      ::write(serial_fd_, mode_write_frame_.data(), MODE_WRITE_FRAME_SIZE) !=
      static_cast<ssize_t>(MODE_WRITE_FRAME_SIZE)) {
      RCLCPP_WARN(
        rclcpp::get_logger("InspireRH56E2HandHardwareInterface"),
        "Failed to send mode write frame");
      return hardware_interface::return_type::ERROR;
    }

    // Read ACK
    if (!read_exact(mode_write_ack_buf_.data(), MODE_WRITE_ACK_SIZE)) {
      RCLCPP_WARN(
        rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Failed to read mode write ACK");
      return hardware_interface::return_type::ERROR;
    }

    if (
      mode_write_ack_buf_[0] != 0x90 || mode_write_ack_buf_[1] != 0xEB ||
      mode_write_ack_buf_[2] != hand_id_ || mode_write_ack_buf_[4] != 0x12) {
      RCLCPP_WARN(
        rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Invalid mode write ACK");
      return hardware_interface::return_type::ERROR;
    }

    last_written_modes_ = mode_write_buf_;
  }

  // Convert URDF joint commands to hardware register values (pre-allocated buffer)
  for (size_t urdf_idx = 0; urdf_idx < NUM_JOINTS; ++urdf_idx) {
    size_t hw_idx = urdf_to_hw_index(urdf_idx);
    angle_write_buf_[hw_idx] = position_to_hardware_value(hw_commands_[urdf_idx], urdf_idx);
  }

  // Skip serial I/O if commands haven't changed since last successful write
  if (angle_write_buf_ != last_written_angles_) {
    // Update data portion of pre-built write frame
    for (size_t i = 0; i < NUM_JOINTS; ++i) {
      angle_write_frame_[7 + i * 2] = static_cast<uint8_t>(angle_write_buf_[i] & 0xFF);
      angle_write_frame_[7 + i * 2 + 1] = static_cast<uint8_t>((angle_write_buf_[i] >> 8) & 0xFF);
    }

    // Recalculate checksum (bytes from index 2 to end-1)
    uint32_t sum = 0;
    for (size_t i = 2; i < ANGLE_WRITE_FRAME_SIZE - 1; ++i) {
      sum += angle_write_frame_[i];
    }
    angle_write_frame_[ANGLE_WRITE_FRAME_SIZE - 1] = static_cast<uint8_t>(sum & 0xFF);

    // Send pre-built frame
    if (
      ::write(serial_fd_, angle_write_frame_.data(), ANGLE_WRITE_FRAME_SIZE) !=
      static_cast<ssize_t>(ANGLE_WRITE_FRAME_SIZE)) {
      RCLCPP_WARN(
        rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Failed to send write frame");
      return hardware_interface::return_type::ERROR;
    }

    // Read ACK using robust read (pre-allocated buffer)
    if (!read_exact(angle_write_ack_buf_.data(), ANGLE_WRITE_ACK_SIZE)) {
      RCLCPP_WARN(
        rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Failed to read write ACK");
      return hardware_interface::return_type::ERROR;
    }

    if (
      angle_write_ack_buf_[0] != 0x90 || angle_write_ack_buf_[1] != 0xEB ||
      angle_write_ack_buf_[2] != hand_id_ || angle_write_ack_buf_[4] != 0x12) {
      RCLCPP_WARN(rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Invalid write ACK");
      return hardware_interface::return_type::ERROR;
    }

    // Remember last written values for change detection
    last_written_angles_ = angle_write_buf_;
  }

  // Write FORCE_SET registers
  for (size_t urdf_idx = 0; urdf_idx < NUM_JOINTS; ++urdf_idx) {
    size_t hw_idx = urdf_to_hw_index(urdf_idx);
    double clamped = std::max(0.0, std::min(3000.0, hw_force_commands_[urdf_idx]));
    force_write_buf_[hw_idx] = static_cast<int16_t>(clamped);
  }

  // Skip serial I/O if force commands haven't changed
  if (force_write_buf_ != last_written_forces_) {
    // Update data portion of pre-built force write frame
    for (size_t i = 0; i < NUM_JOINTS; ++i) {
      force_write_frame_[7 + i * 2] = static_cast<uint8_t>(force_write_buf_[i] & 0xFF);
      force_write_frame_[7 + i * 2 + 1] = static_cast<uint8_t>((force_write_buf_[i] >> 8) & 0xFF);
    }

    // Recalculate checksum
    uint32_t fsum = 0;
    for (size_t i = 2; i < FORCE_WRITE_FRAME_SIZE - 1; ++i) {
      fsum += force_write_frame_[i];
    }
    force_write_frame_[FORCE_WRITE_FRAME_SIZE - 1] = static_cast<uint8_t>(fsum & 0xFF);

    // Send force write frame
    if (
      ::write(serial_fd_, force_write_frame_.data(), FORCE_WRITE_FRAME_SIZE) !=
      static_cast<ssize_t>(FORCE_WRITE_FRAME_SIZE)) {
      RCLCPP_WARN(
        rclcpp::get_logger("InspireRH56E2HandHardwareInterface"),
        "Failed to send force write frame");
      return hardware_interface::return_type::ERROR;
    }

    // Read ACK
    if (!read_exact(force_write_ack_buf_.data(), FORCE_WRITE_ACK_SIZE)) {
      RCLCPP_WARN(
        rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Failed to read force write ACK");
      return hardware_interface::return_type::ERROR;
    }

    if (
      force_write_ack_buf_[0] != 0x90 || force_write_ack_buf_[1] != 0xEB ||
      force_write_ack_buf_[2] != hand_id_ || force_write_ack_buf_[4] != 0x12) {
      RCLCPP_WARN(
        rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Invalid force write ACK");
      return hardware_interface::return_type::ERROR;
    }

    last_written_forces_ = force_write_buf_;
  }

  return hardware_interface::return_type::OK;
}

// RS-485 Communication Implementation (same protocol as RH56)

bool InspireRH56E2HandHardwareInterface::open_serial_port()
{
  serial_fd_ = ::open(serial_port_.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
  if (serial_fd_ < 0) {
    RCLCPP_ERROR(
      rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Error opening %s: %s",
      serial_port_.c_str(), strerror(errno));
    return false;
  }

  struct termios tty;
  if (::tcgetattr(serial_fd_, &tty) != 0) {
    RCLCPP_ERROR(
      rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Error from tcgetattr: %s",
      strerror(errno));
    return false;
  }

  // Set baud rate (supported: 115200, 57600, 19200, 921600 per register 1002)
  speed_t speed;
  if (baudrate_ == 115200)
    speed = B115200;
  else if (baudrate_ == 57600)
    speed = B57600;
  else if (baudrate_ == 19200)
    speed = B19200;
  else if (baudrate_ == 921600)
    speed = B921600;
  else {
    RCLCPP_ERROR(
      rclcpp::get_logger("InspireRH56E2HandHardwareInterface"),
      "Unsupported baudrate: %d. Supported: 115200, 57600, 19200, 921600", baudrate_);
    return false;
  }

  cfsetospeed(&tty, speed);
  cfsetispeed(&tty, speed);

  // 8N1
  tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
  tty.c_iflag &= ~IGNBRK;
  tty.c_lflag = 0;
  tty.c_oflag = 0;
  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 5;

  tty.c_iflag &= ~(IXON | IXOFF | IXANY);
  tty.c_cflag |= (CLOCAL | CREAD);
  tty.c_cflag &= ~(PARENB | PARODD);
  tty.c_cflag &= ~CSTOPB;
  tty.c_cflag &= ~CRTSCTS;

  if (::tcsetattr(serial_fd_, TCSANOW, &tty) != 0) {
    RCLCPP_ERROR(
      rclcpp::get_logger("InspireRH56E2HandHardwareInterface"), "Error from tcsetattr: %s",
      strerror(errno));
    return false;
  }

  return true;
}

void InspireRH56E2HandHardwareInterface::close_serial_port()
{
  if (serial_fd_ >= 0) {
    ::close(serial_fd_);
    serial_fd_ = -1;
  }
}

bool InspireRH56E2HandHardwareInterface::read_exact(uint8_t * buf, size_t count)
{
  size_t total = 0;
  while (total < count) {
    ssize_t n = ::read(serial_fd_, buf + total, count - total);
    if (n <= 0) {
      return false;
    }
    total += static_cast<size_t>(n);
  }
  return true;
}

int16_t InspireRH56E2HandHardwareInterface::position_to_hardware_value(
  double position, size_t joint_index)
{
  // Clamp position to joint limits
  double clamped_pos =
    std::max(joint_min_limits_[joint_index], std::min(joint_max_limits_[joint_index], position));

  // Normalize to 0-1 range
  double range = joint_max_limits_[joint_index] - joint_min_limits_[joint_index];
  double normalized = (clamped_pos - joint_min_limits_[joint_index]) / range;

  // Convert to 0-1000 hardware range with inversion
  return static_cast<int16_t>(1000.0 - normalized * 1000.0);
}

double InspireRH56E2HandHardwareInterface::hardware_value_to_position(
  int16_t hw_value, size_t joint_index)
{
  // Clamp hardware value to 0-1000 range
  double clamped_hw = std::max(0.0, std::min(1000.0, static_cast<double>(hw_value)));

  // Normalize to 0-1 range with inversion
  double normalized = (1000.0 - clamped_hw) / 1000.0;

  // Convert to joint position range
  double range = joint_max_limits_[joint_index] - joint_min_limits_[joint_index];
  return joint_min_limits_[joint_index] + normalized * range;
}

}  // namespace inspire_rh56e2_hand_ros2_control

#include "pluginlib/class_list_macros.hpp"
PLUGINLIB_EXPORT_CLASS(
  inspire_rh56e2_hand_ros2_control::InspireRH56E2HandHardwareInterface,
  hardware_interface::SystemInterface)
