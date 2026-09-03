// SPDX-License-Identifier: Apache-2.0

// Exercise the production lifecycle and read path using a PTY, never a physical hand.
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "inspire_rh56e2_hand_ros2_control/inspire_rh56e2_hand_hardware_interface.hpp"

namespace
{

using Driver = inspire_rh56e2_hand_ros2_control::InspireRH56E2HandHardwareInterface;
using CallbackReturn = hardware_interface::CallbackReturn;
constexpr std::array<uint16_t, 6> kForces{13, 10, 128, 255, 17, 19};

void require(bool condition, const std::string & message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class FileDescriptor
{
public:
  explicit FileDescriptor(int fd = -1) : fd_(fd) {}
  ~FileDescriptor() { reset(); }
  FileDescriptor(const FileDescriptor &) = delete;
  FileDescriptor & operator=(const FileDescriptor &) = delete;
  int get() const { return fd_; }
  void reset(int fd = -1)
  {
    if (fd_ >= 0) {
      ::close(fd_);
    }
    fd_ = fd;
  }

private:
  int fd_;
};

class PtyHand
{
public:
  PtyHand() : master_(::posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC))
  {
    require(master_.get() >= 0, "create PTY master");
    require(::grantpt(master_.get()) == 0 && ::unlockpt(master_.get()) == 0, "unlock PTY");
    const char * name = ::ptsname(master_.get());
    require(name != nullptr, "resolve PTY slave");
    path = name;
    slave_.reset(::open(path.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC));
    require(slave_.get() >= 0, "open PTY slave for inherited termios settings");
    termios tty{};
    require(::tcgetattr(slave_.get(), &tty) == 0, "read inherited termios");
    tty.c_iflag |= ICRNL | INLCR | IGNCR | ISTRIP | PARMRK | INPCK | IXON | IXOFF | IXANY;
    tty.c_lflag |= ICANON | ECHO | ISIG;
    tty.c_oflag |= OPOST;
    require(::tcsetattr(slave_.get(), TCSANOW, &tty) == 0, "set hostile inherited flags");
    worker_ = std::thread([this]() {
      try {
        respond();
      } catch (...) {
        worker_error_ = std::current_exception();
      }
    });
  }

  ~PtyHand() { stop(); }

  void check_configuration() const
  {
    termios tty{};
    require(::tcgetattr(slave_.get(), &tty) == 0, "read configured termios");
    require(
      (tty.c_iflag & (ICRNL | INLCR | IGNCR | ISTRIP | PARMRK | IXON | IXOFF | IXANY)) == 0,
      "binary input must not translate, strip, mark or flow-control bytes");
    require((tty.c_lflag & (ICANON | ECHO | ISIG)) == 0, "disable terminal line processing");
    require((tty.c_oflag & OPOST) == 0, "disable output processing");
    require(
      (tty.c_cflag & CSIZE) == CS8 && (tty.c_cflag & (PARENB | CSTOPB | CRTSCTS)) == 0,
      "retain raw 8N1 without hardware flow control");
    require(
      ::cfgetispeed(&tty) == B115200 && ::cfgetospeed(&tty) == B115200 &&
        tty.c_cc[VMIN] == 0 && tty.c_cc[VTIME] == 5,
      "retain configured baudrate and existing read timeout");
  }

  void finish()
  {
    stop();
    if (worker_error_) {
      std::rethrow_exception(worker_error_);
    }
  }

  std::string path;

private:
  void stop()
  {
    stopping_ = true;
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  bool read_exact(uint8_t * data, std::size_t length)
  {
    std::size_t received = 0;
    while (received < length && !stopping_) {
      pollfd event{master_.get(), POLLIN, 0};
      const int ready = ::poll(&event, 1, 50);
      require(ready >= 0, "poll mock hand request");
      if (ready == 0) {
        continue;
      }
      const ssize_t count = ::read(master_.get(), data + received, length - received);
      require(count > 0, "read mock hand request");
      received += static_cast<std::size_t>(count);
    }
    return received == length;
  }

  void respond()
  {
    while (!stopping_) {
      std::array<uint8_t, 7> request{};
      if (!read_exact(request.data(), request.size())) {
        return;
      }
      require(request[0] == 0xEB && request[1] == 0x90, "binary request header is intact");
      require(request[3] >= 3 && request[3] <= 15, "expected register request length");
      std::vector<uint8_t> remaining(static_cast<std::size_t>(request[3]) - 2);
      if (!read_exact(remaining.data(), remaining.size())) {
        return;
      }
      const bool read = request[4] == 0x11;
      require(read || request[4] == 0x12, "expected read or write command");
      std::vector<uint8_t> response(read ? 20 : 9, 0);
      response[0] = 0x90;
      response[1] = 0xEB;
      response[2] = request[2];
      response[3] = read ? 15 : 4;
      response[4] = request[4];
      response[5] = request[5];
      response[6] = request[6];
      if (read) {
        const uint16_t address = request[5] | (static_cast<uint16_t>(request[6]) << 8);
        require(address == 0x060A || address == 0x062E, "expected angle or force read");
        for (std::size_t index = 0; index < kForces.size(); ++index) {
          const uint16_t value = address == 0x060A ? 1000 : kForces[index];
          response[7 + index * 2] = static_cast<uint8_t>(value & 0xFF);
          response[8 + index * 2] = static_cast<uint8_t>(value >> 8);
        }
      } else {
        response[7] = 1;
      }
      for (std::size_t index = 2; index + 1 < response.size(); ++index) {
        response.back() = static_cast<uint8_t>(response.back() + response[index]);
      }
      require(
        ::write(master_.get(), response.data(), response.size()) ==
          static_cast<ssize_t>(response.size()),
        "write mock hand response");
    }
  }

  FileDescriptor master_;
  FileDescriptor slave_;
  std::atomic<bool> stopping_{false};
  std::exception_ptr worker_error_;
  std::thread worker_;
};

void initialize(Driver & driver, const std::string & port, const std::string & baudrate = "115200")
{
  hardware_interface::HardwareComponentInterfaceParams params;
  auto & info = params.hardware_info;
  info.name = "serial_pty_test";
  info.type = "system";
  info.hardware_parameters = {{"serial_port", port}, {"baudrate", baudrate}};
  for (std::size_t index = 0; index < kForces.size(); ++index) {
    hardware_interface::ComponentInfo joint;
    joint.name = "joint_" + std::to_string(index);
    joint.type = "joint";
    hardware_interface::InterfaceInfo position;
    position.name = "position";
    position.min = "0.0";
    position.max = "1.0";
    joint.command_interfaces.push_back(position);
    info.joints.push_back(joint);
  }
  require(driver.on_init(params) == CallbackReturn::SUCCESS, "initialize real hardware plugin");
}

std::size_t descriptor_count()
{
  DIR * directory = ::opendir("/proc/self/fd");
  require(directory != nullptr, "inspect descriptor cleanup");
  std::size_t count = 0;
  while (::readdir(directory) != nullptr) {
    ++count;
  }
  ::closedir(directory);
  return count;
}

void expect_activation_failure(const std::string & port, const std::string & baudrate)
{
  Driver driver;
  initialize(driver, port, baudrate);
  const auto before = descriptor_count();
  require(driver.on_activate(rclcpp_lifecycle::State()) == CallbackReturn::ERROR,
    "invalid configuration fails activation");
  require(descriptor_count() == before, "failed activation closes its serial descriptor");
  require(driver.on_deactivate(rclcpp_lifecycle::State()) == CallbackReturn::SUCCESS,
    "deactivate after failed activation");
  require(descriptor_count() == before, "failed descriptor is reset before deactivation");
}

}  // namespace

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  int result = 0;
  try {
    PtyHand hand;
    Driver driver;
    initialize(driver, hand.path);
    const auto before = descriptor_count();
    require(driver.on_activate(rclcpp_lifecycle::State()) == CallbackReturn::SUCCESS,
      "activate with hostile termios flags");
    hand.check_configuration();
    require(
      driver.read(rclcpp::Time(0), rclcpp::Duration::from_seconds(0.025)) ==
        hardware_interface::return_type::OK,
      "read binary angle and force responses");
    const auto states = driver.export_state_interfaces();
    for (std::size_t index = 0; index < kForces.size(); ++index) {
      const auto force = states[index * 3 + 2].get_optional<double>();
      require(
        force.has_value() && *force == kForces[kForces.size() - 1 - index],
        "preserve CR, LF, high-bit and XON/XOFF bytes in force feedback");
    }
    require(driver.on_deactivate(rclcpp_lifecycle::State()) == CallbackReturn::SUCCESS,
      "deactivate PTY hand");
    require(descriptor_count() == before, "successful lifecycle releases serial descriptor");
    hand.finish();
    expect_activation_failure(hand.path, "12345");
    expect_activation_failure("/dev/null", "115200");
    expect_activation_failure("/not-a-real-hand/serial-port", "115200");
    std::cout << "PASS: production binary serial activation/read and failure cleanup\n";
  } catch (const std::exception & error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    result = 1;
  }
  rclcpp::shutdown();
  return result;
}
