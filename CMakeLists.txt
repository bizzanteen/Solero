cmake_minimum_required(VERSION 3.25)
project(solero VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Homebrew packages Qt modules in separate cellars (qtbase, qtsvg, ...). Add their
# opt prefixes so a clean reconfigure finds Qt6 (incl. Qt6::Svg) without -D flags.
# qtbase first (it provides moc/rcc tools). WebEngineWidgets pulls in Quick
# (qtdeclarative), WebChannel and Positioning, so add those cellars too. (The
# umbrella opt/qt is NOT used - its CoreTools target references a missing syncqt.)
foreach(_qtopt qtbase qtsvg qtwebengine qtdeclarative qtwebchannel qtpositioning mesa)
  if(EXISTS "/home/linuxbrew/.linuxbrew/opt/${_qtopt}")
    list(APPEND CMAKE_PREFIX_PATH "/home/linuxbrew/.linuxbrew/opt/${_qtopt}")
  endif()
endforeach()
# qtsvg's cmake config lives in its own cellar; point at it directly so the Svg
# component resolves on a clean reconfigure.
if(NOT Qt6Svg_DIR AND EXISTS "/home/linuxbrew/.linuxbrew/opt/qtsvg/lib/cmake/Qt6Svg")
  set(Qt6Svg_DIR "/home/linuxbrew/.linuxbrew/opt/qtsvg/lib/cmake/Qt6Svg")
endif()
# WebEngineWidgets + its deps each keep their cmake config in their own cellar
# (the keg-linked lib/cmake/Qt6Quick is incomplete). Point each _DIR at the cellar
# path that actually contains the Config.cmake so a clean reconfigure resolves them.
set(_solero_qt_dirs
  "Qt6Quick=opt/qtdeclarative/lib/cmake/Qt6Quick"
  "Qt6QuickWidgets=opt/qtdeclarative/lib/cmake/Qt6QuickWidgets"
  "Qt6Qml=opt/qtdeclarative/lib/cmake/Qt6Qml"
  "Qt6QmlModels=opt/qtdeclarative/lib/cmake/Qt6QmlModels"
  "Qt6QmlIntegration=opt/qtdeclarative/lib/cmake/Qt6QmlIntegration"
  "Qt6OpenGL=opt/qtbase/lib/cmake/Qt6OpenGL"
  "Qt6Concurrent=opt/qtbase/lib/cmake/Qt6Concurrent"
  "Qt6WebChannel=opt/qtwebchannel/lib/cmake/Qt6WebChannel"
  "Qt6Positioning=opt/qtpositioning/lib/cmake/Qt6Positioning"
  "Qt6WebEngineCore=opt/qtwebengine/lib/cmake/Qt6WebEngineCore"
  "Qt6WebEngineWidgets=opt/qtwebengine/lib/cmake/Qt6WebEngineWidgets")
foreach(_pair IN LISTS _solero_qt_dirs)
  string(REPLACE "=" ";" _kv "${_pair}")
  list(GET _kv 0 _mod)
  list(GET _kv 1 _rel)
  if(NOT ${_mod}_DIR AND EXISTS "/home/linuxbrew/.linuxbrew/${_rel}")
    set(${_mod}_DIR "/home/linuxbrew/.linuxbrew/${_rel}")
  endif()
endforeach()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Xml Svg Test Concurrent WebEngineWidgets)

# nlohmann/json header-only
include(FetchContent)
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
  DOWNLOAD_NO_EXTRACT TRUE
  DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/include/nlohmann
)
FetchContent_MakeAvailable(json)

# libloot C++ wrapper (built from local Rust source clone; reuses cargo cache).
# Requires the Rust toolchain (cargo) on PATH at build time.
set(LIBLOOT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LIBLOOT_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libloot
  SOURCE_DIR /var/home/eamon/dev/libloot-src
  SOURCE_SUBDIR cpp)
FetchContent_MakeAvailable(libloot)

set(SOLERO_SOURCES
  src/main.cpp
  src/app/Application.cpp
  src/app/SingleInstance.cpp
  src/app/NxmRegister.cpp
  src/core/ModList.cpp
  src/core/PluginList.cpp
  src/core/LoadOrderBackup.cpp
  src/core/Profile.cpp
  src/core/ProfileManager.cpp
  src/ai/AITransaction.cpp
  src/ipc/IPCServer.cpp
  src/ui/MainWindow.cpp
  src/ui/ModListModel.cpp
  src/ui/ModListView.cpp
  src/ui/PluginListModel.cpp
  src/ui/PluginListView.cpp
  src/ui/BottomPanel.cpp
  src/ui/ModInfoWidget.cpp
  src/ui/SeparatorDialog.cpp
  src/ui/ExecutableDialog.cpp
  src/ui/SetupWizard.cpp
  src/ui/SetupPanel.cpp
  src/ui/SettingsDialog.cpp
  src/ui/WabbajackDialog.cpp
  src/core/AppConfig.cpp
  src/deploy/Linker.cpp
  src/deploy/DeployRecord.cpp
  src/deploy/ConflictIndex.cpp
  src/deploy/DeployEngine.cpp
  src/ui/RightPane.cpp
  src/ui/ProgressModal.cpp
  src/ui/DownloadsTab.cpp
  src/ui/DataTab.cpp
  src/ui/ConflictsTab.cpp
  src/ui/FileEditorDialog.cpp
  src/ui/ModFileTree.cpp
  src/ui/YamlHighlighter.cpp
  src/ui/LootRulesEditor.cpp
  src/loot/LootSorter.cpp
  src/bethini/BethiniData.cpp
  src/bethini/BethiniWindow.cpp
  src/bethini/IniFile.cpp
  src/install/ArchiveTool.cpp
  src/install/DataDirDetector.cpp
  src/install/ModInstaller.cpp
  src/fomod/FomodCondition.cpp
  src/fomod/FomodEngine.cpp
  src/ui/FomodWizard.cpp
  src/tools/ToolStore.cpp
  src/tools/ToolRunner.cpp
  src/tools/ToolCatalog.cpp
  src/tools/ToolDownloader.cpp
  src/nexus/NxmHandler.cpp
  src/nexus/NexusApi.cpp
  src/nexus/DownloadManager.cpp
  src/ui/NexusWebView.cpp
  src/ui/ToolPanel.cpp
  src/ui/ToolSetupWizard.cpp
  src/ui/ToolsManagerDialog.cpp
  src/ui/ThemeAdapter.cpp
  src/ui/ThemeAdapter.h
  src/install/DependencyChecker.cpp
  src/install/PluginScanner.cpp
  src/import/Mo2Importer.cpp
  src/wabbajack/WabbajackEngine.cpp
  resources/resources.qrc
)

add_executable(solero ${SOLERO_SOURCES})
target_include_directories(solero PRIVATE src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(solero PRIVATE Qt6::Core Qt6::Widgets Qt6::Network Qt6::Xml Qt6::Svg Qt6::Concurrent Qt6::WebEngineWidgets libloot::libloot)

enable_testing()
add_subdirectory(tests)
