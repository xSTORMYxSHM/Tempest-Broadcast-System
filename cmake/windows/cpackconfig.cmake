# OBS CMake Windows CPack configuration module

include_guard(GLOBAL)

include(cpackconfig_common)

# Add GPLv2 license file to CPack
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/frontend/data/license/gplv2.txt")
set(CPACK_PACKAGE_VERSION "${TEMPEST_PRODUCT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-windows-${CMAKE_VS_PLATFORM_NAME}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY FALSE)
set(CPACK_GENERATOR ZIP)
set(CPACK_THREADS 0)

install(
  FILES "${CMAKE_SOURCE_DIR}/COPYING" "${CMAKE_SOURCE_DIR}/AUTHORS" "${CMAKE_SOURCE_DIR}/NOTICE.txt"
        "${CMAKE_SOURCE_DIR}/PUBLIC_RELEASE.md"
        "${CMAKE_SOURCE_DIR}/RELEASE_NOTES_${TEMPEST_PRODUCT_VERSION}.md"
  DESTINATION "."
  COMPONENT Runtime
)

set(
  _tempest_component_licenses
  "deps/blake2/LICENSE.blake2"
  "deps/json11/LICENSE.txt"
  "deps/libcaption/LICENSE.txt"
  "deps/libdshowcapture/src/COPYING"
  "deps/libdshowcapture/src/external/capture-device-support/LICENSE"
  "deps/w32-pthreads/COPYING"
  "deps/w32-pthreads/COPYING.LIB"
  "libobs/graphics/libnsgif/LICENSE.libnsgif"
  "plugins/decklink/LICENSE.decklink-sdk"
  "plugins/obs-browser/COPYING"
  "plugins/obs-filters/rnnoise/COPYING"
  "plugins/obs-outputs/librtmp/COPYING"
  "plugins/obs-qsv11/obs-qsv11-LICENSE.txt"
  "plugins/obs-qsv11/QSV11-License-Clarification-Email.txt"
  "plugins/obs-websocket/LICENSE"
  "shared/media-playback/LICENSE.media-playback"
)
foreach(_tempest_license IN LISTS _tempest_component_licenses)
  cmake_path(GET _tempest_license PARENT_PATH _tempest_license_directory)
  install(
    FILES "${CMAKE_SOURCE_DIR}/${_tempest_license}"
    DESTINATION "licenses/source-components/${_tempest_license_directory}"
    COMPONENT Runtime
  )
endforeach()

foreach(_tempest_dependency_prefix IN LISTS CMAKE_PREFIX_PATH)
  if(EXISTS "${_tempest_dependency_prefix}/licenses")
    install(
      DIRECTORY "${_tempest_dependency_prefix}/licenses/"
      DESTINATION "licenses/build-dependencies"
      COMPONENT Runtime
    )
  endif()
endforeach()

if(CEF_ROOT_DIR AND EXISTS "${CEF_ROOT_DIR}/LICENSE.txt")
  install(
    FILES "${CEF_ROOT_DIR}/LICENSE.txt"
    DESTINATION "licenses/build-dependencies/cef"
    COMPONENT Runtime
  )
endif()

unset(_tempest_component_licenses)
unset(_tempest_license)
unset(_tempest_license_directory)
unset(_tempest_dependency_prefix)

include(CPack)
