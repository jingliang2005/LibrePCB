# Try to find shared library on the system
find_package(OpenCASCADE 7.5.0 QUIET)
if(OpenCASCADE_FOUND)
  message(STATUS "Using system OpenCASCADE")

  include_directories(${OpenCASCADE_INCLUDE_DIR})
  MESSAGE("OCC-Include-Dir: ${OpenCASCADE_INCLUDE_DIR}")
  link_directories(${OpenCASCADE_LIBRARY_DIR})
  MESSAGE("OCC-Lib-Dir: ${OpenCASCADE_LIBRARY_DIR}")
  MESSAGE("OCC-Libs: ${OpenCASCADE_LIBRARIES}")

  # Stop here, we're done
  return()
endif()

message(FATAL_ERROR "Did not find OpenCASCADE library!")
