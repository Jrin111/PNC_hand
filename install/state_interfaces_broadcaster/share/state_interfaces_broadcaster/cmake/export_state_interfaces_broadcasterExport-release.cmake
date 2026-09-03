#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "state_interfaces_broadcaster::state_interfaces_broadcaster" for configuration "Release"
set_property(TARGET state_interfaces_broadcaster::state_interfaces_broadcaster APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(state_interfaces_broadcaster::state_interfaces_broadcaster PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libstate_interfaces_broadcaster.so"
  IMPORTED_SONAME_RELEASE "libstate_interfaces_broadcaster.so"
  )

list(APPEND _cmake_import_check_targets state_interfaces_broadcaster::state_interfaces_broadcaster )
list(APPEND _cmake_import_check_files_for_state_interfaces_broadcaster::state_interfaces_broadcaster "${_IMPORT_PREFIX}/lib/libstate_interfaces_broadcaster.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
