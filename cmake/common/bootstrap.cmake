# OBS CMake bootstrap module

include_guard(GLOBAL)

# Map fallback configurations for optimized build configurations
# gersemi: off
set(
  CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO
    RelWithDebInfo
    Release
    MinSizeRel
    None
    ""
)
set(
  CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL
    MinSizeRel
    Release
    RelWithDebInfo
    None
    ""
)
set(
  CMAKE_MAP_IMPORTED_CONFIG_RELEASE
    Release
    RelWithDebInfo
    MinSizeRel
    None
    ""
)
# gersemi: on

# Prohibit in-source builds
if("${CMAKE_CURRENT_BINARY_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
  message(
    FATAL_ERROR
    "In-source builds of OBS are not supported. "
    "Specify a build directory via 'cmake -S <SOURCE DIRECTORY> -B <BUILD_DIRECTORY>' instead."
  )
  file(REMOVE_RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/CMakeCache.txt" "${CMAKE_CURRENT_SOURCE_DIR}/CMakeFiles")
endif()

# Set default global project variables
set(OBS_COMPANY_NAME "Tempest Mainframe")
set(OBS_PRODUCT_NAME "Tempest Broadcast System")
set(OBS_WEBSITE "https://www.obsproject.com")
set(OBS_COMMENTS "Open-source Tempest Mainframe broadcast workstation, built on OBS Studio")
set(OBS_LEGAL_COPYRIGHT "(C) Tempest Mainframe contributors")
set(TEMPEST_PRODUCT_VERSION "1.0.1" CACHE STRING "Tempest Broadcast product version")

if(NOT TEMPEST_PRODUCT_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
  message(FATAL_ERROR "TEMPEST_PRODUCT_VERSION must begin with a numeric major.minor.patch version")
endif()
set(TEMPEST_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(TEMPEST_VERSION_MINOR "${CMAKE_MATCH_2}")
set(TEMPEST_VERSION_PATCH "${CMAKE_MATCH_3}")
set(OBS_CMAKE_VERSION 3.0.0)

# Configure default version strings
set(_obs_default_version "0" "0" "1")
set(_obs_release_candidate 0)
set(_obs_beta 0)

# Add common module directories to default search path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/common" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/finders")

include(policies NO_POLICY_SCOPE)
include(versionconfig)
include(buildnumber)
include(osconfig)

# Allow selection of common build types via UI
if(NOT CMAKE_GENERATOR MATCHES "(Xcode|Visual Studio .+)")
  if(NOT CMAKE_BUILD_TYPE)
    set(
      CMAKE_BUILD_TYPE
      "RelWithDebInfo"
      CACHE STRING
      "OBS build type [Release, RelWithDebInfo, Debug, MinSizeRel]"
      FORCE
    )
    set_property(
      CACHE CMAKE_BUILD_TYPE
      PROPERTY STRINGS Release RelWithDebInfo Debug MinSizeRel
    )
  endif()
endif()

# Enable default inclusion of targets' source and binary directory
set(CMAKE_INCLUDE_CURRENT_DIR TRUE)
