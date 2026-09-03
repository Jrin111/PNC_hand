// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined _WIN32 || defined __CYGWIN__
  #ifdef __GNUC__
    #define SSC_TACTILE_HAND_ROS2_CONTROL_EXPORT __attribute__((dllexport))
    #define SSC_TACTILE_HAND_ROS2_CONTROL_IMPORT __attribute__((dllimport))
  #else
    #define SSC_TACTILE_HAND_ROS2_CONTROL_EXPORT __declspec(dllexport)
    #define SSC_TACTILE_HAND_ROS2_CONTROL_IMPORT __declspec(dllimport)
  #endif
  #ifdef SSC_TACTILE_HAND_ROS2_CONTROL_BUILDING_DLL
    #define SSC_TACTILE_HAND_ROS2_CONTROL_PUBLIC SSC_TACTILE_HAND_ROS2_CONTROL_EXPORT
  #else
    #define SSC_TACTILE_HAND_ROS2_CONTROL_PUBLIC SSC_TACTILE_HAND_ROS2_CONTROL_IMPORT
  #endif
  #define SSC_TACTILE_HAND_ROS2_CONTROL_LOCAL
#else
  #define SSC_TACTILE_HAND_ROS2_CONTROL_EXPORT __attribute__((visibility("default")))
  #define SSC_TACTILE_HAND_ROS2_CONTROL_IMPORT
  #if __GNUC__ >= 4
    #define SSC_TACTILE_HAND_ROS2_CONTROL_PUBLIC __attribute__((visibility("default")))
    #define SSC_TACTILE_HAND_ROS2_CONTROL_LOCAL __attribute__((visibility("hidden")))
  #else
    #define SSC_TACTILE_HAND_ROS2_CONTROL_PUBLIC
    #define SSC_TACTILE_HAND_ROS2_CONTROL_LOCAL
  #endif
#endif
