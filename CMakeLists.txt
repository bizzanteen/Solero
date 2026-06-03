cmake_minimum_required(VERSION 3.25)
project(solero VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Test)

# nlohmann/json header-only
include(FetchContent)
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
  DOWNLOAD_NO_EXTRACT TRUE
  DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/include/nlohmann
)
FetchContent_MakeAvailable(json)

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
  src/ui/DataTab.cpp
  src/ui/ConflictsTab.cpp
  src/ui/FileEditorDialog.cpp
  src/ui/ModFileTree.cpp
  resources/resources.qrc
)

add_executable(solero ${SOLERO_SOURCES})
target_include_directories(solero PRIVATE src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(solero PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)

enable_testing()
add_subdirectory(tests)
