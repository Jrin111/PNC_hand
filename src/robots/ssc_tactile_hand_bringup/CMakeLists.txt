cmake_minimum_required(VERSION 3.8)
project(ssc_tactile_hand_bringup)

find_package(ament_cmake REQUIRED)

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(
    NAME bringup_config_test
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/test/bringup_config_test.py
      ${CMAKE_CURRENT_SOURCE_DIR}
  )
endif()

install(
  DIRECTORY
    config
    launch
    urdf
  DESTINATION share/${PROJECT_NAME}
)

ament_package()
