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

#include <algorithm>
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <chrono>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "control_msgs/action/parallel_gripper_command.hpp"
#include "control_msgs/msg/gripper_command.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "sensor_msgs/msg/joint_state.hpp"
#include "std_msgs/msg/float64.hpp"
#include "std_msgs/msg/float64_multi_array.hpp"
#include "std_msgs/msg/string.hpp"
#include "yaml-cpp/yaml.h"

using namespace std::chrono_literals;

struct GripperMapping
{
  double gripper_width;
  std::vector<double> joint_positions;
};

class HandGripperActionAdapter : public rclcpp::Node
{
public:
  using ParallelGripperCommand = control_msgs::action::ParallelGripperCommand;
  using GoalHandleParallelGripperCommand = rclcpp_action::ServerGoalHandle<ParallelGripperCommand>;

  explicit HandGripperActionAdapter(const rclcpp::NodeOptions & options = rclcpp::NodeOptions())
  : Node("hand_gripper_action_adapter", options)
  {
    // Declare parameters with default values
    this->declare_parameter("action_server_name", "hand_gripper_cmd");
    this->declare_parameter("gripper_command_topic", "hand_gripper_command");
    this->declare_parameter("position_controller_topic", "/dexhand_position_controller/commands");
    this->declare_parameter("mapping_config_file", "rh56e2_gripper_joint_mapping.yaml");
    this->declare_parameter("max_width_topic", "gripper_max_width");
    this->declare_parameter("profile_topic", "set_grasp_profile");
    this->declare_parameter("execution_duration", 2.0);  // Hands need more time than grippers

    // Get parameters
    action_server_name_ = this->get_parameter("action_server_name").as_string();
    gripper_command_topic_ = this->get_parameter("gripper_command_topic").as_string();
    position_controller_topic_ = this->get_parameter("position_controller_topic").as_string();
    mapping_config_file_ = this->get_parameter("mapping_config_file").as_string();
    max_width_topic_ = this->get_parameter("max_width_topic").as_string();
    profile_topic_ = this->get_parameter("profile_topic").as_string();
    execution_duration_ = this->get_parameter("execution_duration").as_double();

    // Load gripper-to-joint mapping from YAML file
    if (!load_all_profiles()) {
      RCLCPP_ERROR(this->get_logger(), "Failed to load gripper profiles");
    }

    // Create action server
    action_server_ = rclcpp_action::create_server<ParallelGripperCommand>(
      this, action_server_name_,
      std::bind(
        &HandGripperActionAdapter::handle_goal, this, std::placeholders::_1, std::placeholders::_2),
      std::bind(&HandGripperActionAdapter::handle_cancel, this, std::placeholders::_1),
      std::bind(&HandGripperActionAdapter::handle_accepted, this, std::placeholders::_1));

    // Create publisher for position commands
    position_command_pub_ =
      this->create_publisher<std_msgs::msg::Float64MultiArray>(position_controller_topic_, 10);

    auto max_width_qos = rclcpp::QoS(1).transient_local().reliable();
    max_width_pub_ =
      this->create_publisher<std_msgs::msg::Float64>(max_width_topic_, max_width_qos);
    publish_max_width(max_gripper_width_);

    // Create subscriber for simple gripper commands
    gripper_command_sub_ = this->create_subscription<control_msgs::msg::GripperCommand>(
      gripper_command_topic_, 10,
      std::bind(&HandGripperActionAdapter::gripper_command_callback, this, std::placeholders::_1));

    profile_sub_ = this->create_subscription<std_msgs::msg::String>(
      profile_topic_, 10,
      std::bind(&HandGripperActionAdapter::profile_callback, this, std::placeholders::_1));

    RCLCPP_INFO(this->get_logger(), "Hand Gripper Action Adapter initialized");
    RCLCPP_INFO(this->get_logger(), "Action server: %s", action_server_name_.c_str());
    RCLCPP_INFO(this->get_logger(), "Gripper command topic: %s", gripper_command_topic_.c_str());
    RCLCPP_INFO(this->get_logger(), "Profile switch topic: %s", profile_topic_.c_str());

    RCLCPP_INFO(
      this->get_logger(), "Position controller topic: %s", position_controller_topic_.c_str());
    RCLCPP_INFO(this->get_logger(), "Max width topic: %s", max_width_topic_.c_str());
    RCLCPP_INFO(this->get_logger(), "Max gripper width: %.4f m", max_gripper_width_);
    RCLCPP_INFO(
      this->get_logger(), "Active profile: %s (max_width=%.4f m)", current_profile_.c_str(),
      get_active_max_width());
    RCLCPP_INFO(this->get_logger(), "Execution duration: %.1f seconds", execution_duration_);
  }

private:
  bool load_all_profiles()
  {
    try {
      // Get the path to the config file
      std::string package_share_directory =
        ament_index_cpp::get_package_share_directory("dexhand_utils");
      std::string config_path = package_share_directory + "/config/" + mapping_config_file_;

      RCLCPP_INFO(this->get_logger(), "Loading gripper mapping from: %s", config_path.c_str());

      YAML::Node config = YAML::LoadFile(config_path);

      if (!config["profiles"]) {
        RCLCPP_ERROR(this->get_logger(), "Missing or invalid 'profiles' in config");
        return false;
      }

      profiles_.clear();
      max_widths_.clear();

      // load all profiles
      for (auto it = config["profiles"].begin(); it != config["profiles"].end(); ++it) {
        std::string name = it->first.as<std::string>();
        std::vector<GripperMapping> maps;
        double max_w = 0.0;
        if (!load_profile_node(name, it->second, maps, max_w)) {
          RCLCPP_ERROR(this->get_logger(), "Failed to load profile '%s'", name.c_str());
          return false;
        }
        profiles_[name] = std::move(maps);
        max_widths_[name] = max_w;
      }

      if (profiles_.empty()) {
        RCLCPP_ERROR(this->get_logger(), "No profiles loaded");
        return false;
      }

      std::string default_profile = config["default_profile"]
                                      ? config["default_profile"].as<std::string>()
                                      : profiles_.begin()->first;

      if (profiles_.find(default_profile) == profiles_.end()) {
        RCLCPP_WARN(
          this->get_logger(), "default_profile '%s' not found, using '%s'", default_profile.c_str(),
          profiles_.begin()->first.c_str());
        default_profile = profiles_.begin()->first;
      }
      current_profile_ = default_profile;
      max_gripper_width_ = get_active_max_width();
      return true;
    } catch (const std::exception & e) {
      RCLCPP_ERROR(this->get_logger(), "Exception loading profiles: %s", e.what());
      return false;
    }
  }

  bool load_profile_node(
    const std::string & name, const YAML::Node & node, std::vector<GripperMapping> & out,
    double & max_w)
  {
    // Validate that "mapping" key exists and is a YAML sequence (list)
    if (!node["mapping"] || !node["mapping"].IsSequence()) {
      RCLCPP_ERROR(this->get_logger(), "Profile '%s' missing 'mapping' sequence", name.c_str());
      return false;
    }

    // Clear any previous data in case the output vector was reused
    out.clear();

    // Iterate over each mapping entry: {gripper_width, joint_positions}
    for (const auto & m : node["mapping"]) {
      GripperMapping gm;

      // Parse the scalar gripper_width (in meters)
      gm.gripper_width = m["gripper_width"].as<double>();

      // Validate that joint_positions exists and is a sequence
      if (!m["joint_positions"] || !m["joint_positions"].IsSequence()) {
        RCLCPP_ERROR(this->get_logger(), "Profile '%s' invalid joint_positions", name.c_str());
        return false;
      }

      // Parse each joint position (in radians) into the vector
      for (const auto & p : m["joint_positions"]) {
        gm.joint_positions.push_back(p.as<double>());
      }

      // Enforce 6-joint structure (matches the dexterous hand DOF count)
      if (gm.joint_positions.size() != 6) {
        RCLCPP_ERROR(
          this->get_logger(), "Profile '%s' expects 6 joints, got %zu", name.c_str(),
          gm.joint_positions.size());
        return false;
      }

      // Move-construct into output vector (avoids copy of joint_positions)
      out.push_back(std::move(gm));
    }

    // Sort entries by gripper_width ascending — required for linear interpolation
    // (interpolation logic walks the vector and finds bracket [w_i, w_{i+1}])
    std::sort(out.begin(), out.end(), [](const GripperMapping & a, const GripperMapping & b) {
      return a.gripper_width < b.gripper_width;
    });

    // After sort, last element holds the maximum width — used to clamp goals
    // (returns 0.0 if profile is empty, but we treat empty as failure below)
    max_w = out.empty() ? 0.0 : out.back().gripper_width;

    // Empty profile is considered invalid
    return !out.empty();
  }

  // Profile switching
  void profile_callback(const std_msgs::msg::String::SharedPtr msg)
  {
    double redrive_width = -1.0;
    {
      std::lock_guard<std::mutex> lock(profile_mutex_);

      if (profiles_.find(msg->data) == profiles_.end()) {
        RCLCPP_WARN(this->get_logger(), "Unknown profile '%s'. Available: ", msg->data.c_str());
        for (const auto & [name, _] : profiles_) {
          RCLCPP_WARN(this->get_logger(), "  - %s", name.c_str());
        }
        return;
      }
      if (current_profile_ == msg->data) {
        RCLCPP_DEBUG(this->get_logger(), "Profile '%s' already active", msg->data.c_str());
        return;
      }
      current_profile_ = msg->data;
      max_gripper_width_ = max_widths_.at(current_profile_);
      RCLCPP_INFO(
        this->get_logger(), "Switched to profile: '%s' (max_width=%.4f m)",
        current_profile_.c_str(), max_gripper_width_);
      redrive_width = last_commanded_width_;
    }
    // Re-drive the hand with the last commanded width mapped through the NEW
    // profile. /set_grasp_profile and the width command arrive on different
    // topics with no cross-topic ordering guarantee, so under load the width
    // command can be processed first (mapped with the old profile) and the
    // hand would keep the old posture until the next command.
    if (redrive_width >= 0.0) {
      send_position_command(redrive_width);
    }
  }

  double get_active_max_width() { return max_widths_.at(current_profile_); }

  // Interpolation (uses active profile)
  std::vector<double> interpolate_joint_positions(double gripper_width)
  {
    // Guard against concurrent mutation from mapping_request_callback hot-reloads.
    std::lock_guard<std::mutex> lock(profile_mutex_);

    // Clamp gripper width to valid range
    const auto & gripper_mappings = profiles_.at(current_profile_);
    double max_gripper_width = max_widths_.at(current_profile_);

    gripper_width = std::clamp(gripper_width, 0.0, max_gripper_width);

    // Find the appropriate mapping points for interpolation
    if (gripper_width <= gripper_mappings.front().gripper_width) {
      return gripper_mappings.front().joint_positions;
    }

    if (gripper_width >= gripper_mappings.back().gripper_width) {
      return gripper_mappings.back().joint_positions;
    }

    // Find the two mapping points to interpolate between
    for (size_t i = 0; i < gripper_mappings.size() - 1; ++i) {
      if (
        gripper_width >= gripper_mappings[i].gripper_width &&
        gripper_width <= gripper_mappings[i + 1].gripper_width) {
        const auto & lower = gripper_mappings[i];
        const auto & upper = gripper_mappings[i + 1];

        // Linear interpolation
        double alpha =
          (gripper_width - lower.gripper_width) / (upper.gripper_width - lower.gripper_width);

        std::vector<double> interpolated_positions(6);
        for (size_t j = 0; j < 6; ++j) {
          interpolated_positions[j] = lower.joint_positions[j] +
                                      alpha * (upper.joint_positions[j] - lower.joint_positions[j]);
        }

        return interpolated_positions;
      }
    }

    // Fallback (should not reach here)
    return gripper_mappings.front().joint_positions;
  }

  rclcpp_action::GoalResponse handle_goal(
    const rclcpp_action::GoalUUID & uuid, std::shared_ptr<const ParallelGripperCommand::Goal> goal)
  {
    (void)uuid;

    RCLCPP_INFO(this->get_logger(), "Received hand gripper goal request");

    // Extract position from JointState command (assume single position value)
    double goal_position = 0.0;
    double goal_effort = 0.0;

    if (!goal->command.position.empty()) {
      goal_position = goal->command.position[0];
    }
    if (!goal->command.effort.empty()) {
      goal_effort = goal->command.effort[0];
    }

    double max_width = 0.0;
    std::string profile_name;
    {
      std::lock_guard<std::mutex> lock(profile_mutex_);
      max_width = get_active_max_width();
      profile_name = current_profile_;
    }

    RCLCPP_INFO(
      this->get_logger(), "Goal position: %.4f m, max_effort: %.2f N (active='%s', max=%.4f)",
      goal_position, goal_effort, profile_name.c_str(), max_width);

    // Validate goal parameters
    if (goal_position < 0.0 || goal_position > max_gripper_width_) {
      RCLCPP_WARN(
        this->get_logger(), "Invalid gripper position: %.4f m. Valid range: [0.0, %.4f]",
        goal_position, max_gripper_width_);
      return rclcpp_action::GoalResponse::REJECT;
    }
    if (goal_effort < 0.0) {
      RCLCPP_WARN(this->get_logger(), "Invalid max_effort: %.2f N. Must be >= 0.0", goal_effort);
      return rclcpp_action::GoalResponse::REJECT;
    }

    return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
  }

  rclcpp_action::CancelResponse handle_cancel(
    const std::shared_ptr<GoalHandleParallelGripperCommand> goal_handle)
  {
    RCLCPP_INFO(this->get_logger(), "Received request to cancel hand gripper goal");
    (void)goal_handle;
    return rclcpp_action::CancelResponse::ACCEPT;
  }

  void handle_accepted(const std::shared_ptr<GoalHandleParallelGripperCommand> goal_handle)
  {
    // Execute the goal in a separate thread
    std::thread{
      std::bind(&HandGripperActionAdapter::execute, this, std::placeholders::_1), goal_handle}
      .detach();
  }

  void gripper_command_callback(const control_msgs::msg::GripperCommand::SharedPtr msg)
  {
    // Validate command parameters (snapshot max_gripper_width_ under lock
    // since it can be mutated by mapping_request_callback hot-reloads;
    // release before calling send_position_command, which locks internally).
    double max_gripper_width = 0.0;
    std::string profile_name;
    {
      std::lock_guard<std::mutex> lock(profile_mutex_);
      max_gripper_width = get_active_max_width();
      profile_name = current_profile_;
    }

    RCLCPP_INFO_THROTTLE(
      this->get_logger(), *this->get_clock(), 1000,
      "Received simple hand gripper command: position=%.4f, max_effort=%.2f (profile='%s')",
      msg->position, msg->max_effort, profile_name.c_str());

    // Validate command parameters
    if (msg->position < 0.0 || msg->position > max_gripper_width) {
      RCLCPP_WARN(
        this->get_logger(), "Invalid gripper position: %.4f m. Valid range: [0.0, %.4f]",
        msg->position, max_gripper_width);
      return;
    }

    if (msg->max_effort < 0.0) {
      RCLCPP_WARN(
        this->get_logger(), "Invalid max_effort: %.2f N. Must be >= 0.0", msg->max_effort);
      return;
    }

    // Send position command directly (no action feedback needed for simple commands)
    send_position_command(msg->position);
  }

  void publish_max_width(double max_width)
  {
    auto msg = std_msgs::msg::Float64();
    msg.data = max_width;
    max_width_pub_->publish(msg);
  }

  void send_position_command(double gripper_width)
  {
    {
      std::lock_guard<std::mutex> lock(profile_mutex_);
      last_commanded_width_ = gripper_width;
    }

    // Convert gripper width to 6-joint hand positions using interpolation
    std::vector<double> joint_positions = interpolate_joint_positions(gripper_width);

    // Create and publish position command
    auto position_cmd = std_msgs::msg::Float64MultiArray();
    position_cmd.data = joint_positions;

    RCLCPP_INFO_THROTTLE(
      this->get_logger(), *this->get_clock(), 1000,
      "Sending hand position commands for gripper width %.4f: [%.3f, %.3f, %.3f, %.3f, %.3f, %.3f]",
      gripper_width, joint_positions[0], joint_positions[1], joint_positions[2], joint_positions[3],
      joint_positions[4], joint_positions[5]);

    position_command_pub_->publish(position_cmd);
  }

  void execute(const std::shared_ptr<GoalHandleParallelGripperCommand> goal_handle)
  {
    RCLCPP_INFO(this->get_logger(), "Executing hand gripper goal");

    const auto goal = goal_handle->get_goal();
    auto feedback = std::make_shared<ParallelGripperCommand::Feedback>();
    auto result = std::make_shared<ParallelGripperCommand::Result>();

    // Extract position from JointState command
    double goal_position = 0.0;
    if (!goal->command.position.empty()) {
      goal_position = goal->command.position[0];
    }

    // Send position command using shared function
    send_position_command(goal_position);

    // Mock execution with configurable duration and periodic feedback
    auto start_time = this->now();
    rclcpp::Rate rate(10);          // 10 Hz feedback rate
    double current_position = 0.0;  // Start from closed position

    while (rclcpp::ok()) {
      // Check if goal was canceled
      if (goal_handle->is_canceling()) {
        RCLCPP_INFO(this->get_logger(), "Goal canceled");
        result->state.position = {current_position};
        result->state.effort = {0.0};
        result->stalled = false;
        result->reached_goal = false;
        goal_handle->canceled(result);
        return;
      }

      // Calculate progress (linear interpolation)
      double elapsed_time = (this->now() - start_time).seconds();
      double progress = std::min(elapsed_time / execution_duration_, 1.0);
      current_position = progress * goal_position;

      // Update feedback (using JointState structure)
      feedback->state.position = {current_position};
      feedback->state.effort = {0.0};  // Mock effort feedback
      bool reached_goal = (progress >= 1.0);

      goal_handle->publish_feedback(feedback);

      // Check if goal is reached
      if (reached_goal) {
        RCLCPP_INFO(this->get_logger(), "Goal reached successfully");
        result->state.position = {goal_position};
        result->state.effort = {0.0};
        result->stalled = false;
        result->reached_goal = true;
        goal_handle->succeed(result);
        return;
      }

      rate.sleep();
    }
  }

  // ROS 2 interfaces
  rclcpp_action::Server<ParallelGripperCommand>::SharedPtr action_server_;
  rclcpp::Publisher<std_msgs::msg::Float64MultiArray>::SharedPtr position_command_pub_;
  rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr max_width_pub_;
  rclcpp::Subscription<control_msgs::msg::GripperCommand>::SharedPtr gripper_command_sub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr profile_sub_;

  // Parameters
  std::string action_server_name_;
  std::string gripper_command_topic_;
  std::string position_controller_topic_;
  std::string mapping_config_file_;
  std::string max_width_topic_;
  std::string profile_topic_;
  double execution_duration_;
  double max_gripper_width_;

  // Profile state
  std::unordered_map<std::string, std::vector<GripperMapping>> profiles_;
  std::unordered_map<std::string, double> max_widths_;
  std::string current_profile_;
  // Last width accepted by any command path; < 0 until the first command so a
  // profile switch at startup never moves the hand. Guarded by profile_mutex_.
  double last_commanded_width_ = -1.0;
  std::mutex profile_mutex_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);

  auto node = std::make_shared<HandGripperActionAdapter>();

  RCLCPP_INFO(node->get_logger(), "Starting Hand Gripper Action Adapter node");

  rclcpp::spin(node);

  rclcpp::shutdown();
  return 0;
}
