add_library(tempest-vertical-canvas MODULE)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

target_link_libraries(
  tempest-vertical-canvas
  PRIVATE OBS::libobs OBS::frontend-api Qt::Core Qt::Widgets CURL::libcurl
)

target_compile_definitions(tempest-vertical-canvas PRIVATE TEMPEST_MANAGED_VERTICAL_CANVAS=1)
target_include_directories(tempest-vertical-canvas PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

set(TEMPEST_VERTICAL_CANVAS_VERSION "1.6.4")
set(PROJECT_VERSION "${TEMPEST_VERTICAL_CANVAS_VERSION}")
set(PROJECT_VERSION_MAJOR 1)
set(PROJECT_VERSION_MINOR 6)
set(PROJECT_VERSION_PATCH 4)
configure_file(version.h.in version.h)

target_sources(
  tempest-vertical-canvas
  PRIVATE vertical-canvas.cpp
          scenes-dock.cpp
          sources-dock.cpp
          source-tree.cpp
          transitions-dock.cpp
          qt-display.cpp
          projector.cpp
          config-dialog.cpp
          hotkey-edit.cpp
          name-dialog.cpp
          audio-wrapper-source.c
          file-updater.c
          multi-canvas-source.c
          resources.qrc
          vertical-canvas.hpp
          scenes-dock.hpp
          sources-dock.hpp
          source-tree.hpp
          transitions-dock.hpp
          qt-display.hpp
          projector.hpp
          display-helpers.hpp
          config-dialog.hpp
          hotkey-edit.hpp
          name-dialog.hpp
          audio-wrapper-source.h
          obs-websocket-api.h
          file-updater.h
          multi-canvas-source.h
)

set_target_properties(
  tempest-vertical-canvas
  PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
set_target_properties_obs(tempest-vertical-canvas PROPERTIES FOLDER "plugins/tempest" PREFIX "")
