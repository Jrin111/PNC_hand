# generated from ament/cmake/core/templates/nameConfig.cmake.in

# prevent multiple inclusion
if(_state_interfaces_broadcaster_CONFIG_INCLUDED)
  # ensure to keep the found flag the same
  if(NOT DEFINED state_interfaces_broadcaster_FOUND)
    # explicitly set it to FALSE, otherwise CMake will set it to TRUE
    set(state_interfaces_broadcaster_FOUND FALSE)
  elseif(NOT state_interfaces_broadcaster_FOUND)
    # use separate condition to avoid uninitialized variable warning
    set(state_interfaces_broadcaster_FOUND FALSE)
  endif()
  return()
endif()
set(_state_interfaces_broadcaster_CONFIG_INCLUDED TRUE)

# output package information
if(NOT state_interfaces_broadcaster_FIND_QUIETLY)
  message(STATUS "Found state_interfaces_broadcaster: 4.42.1 (${state_interfaces_broadcaster_DIR})")
endif()

# warn when using a deprecated package
if(NOT "" STREQUAL "")
  set(_msg "Package 'state_interfaces_broadcaster' is deprecated")
  # append custom deprecation text if available
  if(NOT "" STREQUAL "TRUE")
    set(_msg "${_msg} ()")
  endif()
  # optionally quiet the deprecation message
  if(NOT state_interfaces_broadcaster_DEPRECATED_QUIET)
    message(DEPRECATION "${_msg}")
  endif()
endif()

# flag package as ament-based to distinguish it after being find_package()-ed
set(state_interfaces_broadcaster_FOUND_AMENT_PACKAGE TRUE)

# include all config extra files
set(_extras "ament_cmake_export_dependencies-extras.cmake;ament_cmake_export_targets-extras.cmake")
foreach(_extra ${_extras})
  include("${state_interfaces_broadcaster_DIR}/${_extra}")
endforeach()
