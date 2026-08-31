cmake_minimum_required(VERSION 3.28...3.30)

find_package(nlohmann_json 3.11 REQUIRED)

if(NOT TEMPEST_PRODUCT_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
  message(FATAL_ERROR "The Tempest updater requires a numeric major.minor.patch product version")
endif()

set(TEMPEST_UPDATER_VERSION_COMMA "${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},0")
configure_file(tempest-updater.rc.in tempest-updater.rc @ONLY)

add_executable(tempest-broadcast-updater WIN32)

target_sources(
  tempest-broadcast-updater
  PRIVATE main.cpp tempest-updater.manifest "${CMAKE_CURRENT_BINARY_DIR}/tempest-updater.rc"
)

target_compile_features(tempest-broadcast-updater PRIVATE cxx_std_17)
target_compile_definitions(
  tempest-broadcast-updater PRIVATE TEMPEST_PRODUCT_VERSION="${TEMPEST_PRODUCT_VERSION}" WIN32_LEAN_AND_MEAN
)
target_link_libraries(
  tempest-broadcast-updater
  PRIVATE
    nlohmann_json::nlohmann_json
    bcrypt
    crypt32
    ole32
    shell32
    uuid
    version
    winhttp
    wintrust
)

set_target_properties_obs(
  tempest-broadcast-updater
  PROPERTIES
    FOLDER frontend
    OUTPUT_NAME tempest-broadcast-updater
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
