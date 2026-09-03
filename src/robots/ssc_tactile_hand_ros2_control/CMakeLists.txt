cmake_minimum_required(VERSION 3.8)
project(ssc_tactile_hand_ros2_control)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(hardware_interface REQUIRED)
find_package(pluginlib REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)

pkg_check_modules(GPIOD REQUIRED IMPORTED_TARGET libgpiod>=1.6)
if(NOT GPIOD_VERSION VERSION_LESS "2.0")
  message(FATAL_ERROR
    "ssc_tactile_hand_ros2_control currently targets the libgpiod 1.6 C API. "
    "Detected libgpiod ${GPIOD_VERSION}; update the GPIO abstraction before building with 2.x.")
endif()

add_library(${PROJECT_NAME}_hardware_interface SHARED
  src/spi_transport.cpp
  src/ssc_tactile_hand_hardware_interface.cpp
)

target_include_directories(${PROJECT_NAME}_hardware_interface
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_definitions(${PROJECT_NAME}_hardware_interface
  PRIVATE SSC_TACTILE_HAND_ROS2_CONTROL_BUILDING_DLL
)

ament_target_dependencies(${PROJECT_NAME}_hardware_interface
  hardware_interface
  pluginlib
  rclcpp
  rclcpp_lifecycle
)

target_link_libraries(${PROJECT_NAME}_hardware_interface
  PkgConfig::GPIOD
)

if(BUILD_TESTING)
  add_executable(protocol_processing_test
    test/protocol_processing_test.cpp
  )
  target_include_directories(protocol_processing_test
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  )

  # Run the production read/lifecycle code against a test-only SPI transport.
  # No physical device or ROS command publisher is used by this target.
  add_executable(hardware_validity_test
    test/hardware_validity_test.cpp
    src/ssc_tactile_hand_hardware_interface.cpp
  )
  target_include_directories(hardware_validity_test
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
  ament_target_dependencies(hardware_validity_test
    hardware_interface pluginlib rclcpp rclcpp_lifecycle
  )

  if(CMAKE_CROSSCOMPILING)
    find_program(QEMU_AARCH64_EXECUTABLE qemu-aarch64-static)
    if(QEMU_AARCH64_EXECUTABLE AND CMAKE_SYSROOT)
      foreach(test_target protocol_processing_test hardware_validity_test)
        set_property(
          TARGET ${test_target}
          PROPERTY CROSSCOMPILING_EMULATOR
            "${QEMU_AARCH64_EXECUTABLE};-L;${CMAKE_SYSROOT}"
        )
      endforeach()
    else()
      message(WARNING "ARM64 tests cannot run during cross-build: QEMU or sysroot missing")
    endif()
  endif()
  add_test(NAME protocol_processing_test COMMAND protocol_processing_test)
  add_test(NAME hardware_validity_test COMMAND hardware_validity_test)
endif()

pluginlib_export_plugin_description_file(
  hardware_interface
  hardware_interface_plugin.xml
)

install(
  TARGETS ${PROJECT_NAME}_hardware_interface
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES DESTINATION include
)

install(
  DIRECTORY include/
  DESTINATION include
)

install(
  FILES README.md
  DESTINATION share/${PROJECT_NAME}
)

ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_dependencies(
  hardware_interface
  pluginlib
  rclcpp
  rclcpp_lifecycle
)

ament_package()
