cmake_minimum_required(VERSION 3.25)
project(solero VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Xml Test)

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
  src/core/ModList.cpp
  src/core/PluginList.cpp
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
  src/ui/SeparatorDialog.cpp
  src/ui/ExecutableDialog.cpp
  src/ui/SetupWizard.cpp
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
  src/ui/ToolPanel.cpp
  src/ui/ToolSetupWizard.cpp
  src/ui/LeftPane.cpp
  src/ui/ThemeAdapter.cpp
  src/ui/ThemeAdapter.h
  src/install/DependencyChecker.cpp
  src/install/PluginScanner.cpp
  src/import/Mo2Importer.cpp
  resources/resources.qrc
)

add_executable(solero ${SOLERO_SOURCES})
target_include_directories(solero PRIVATE src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(solero PRIVATE Qt6::Core Qt6::Widgets Qt6::Network Qt6::Xml libloot::libloot)

enable_testing()
add_subdirectory(tests)
