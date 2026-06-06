function(add_solero_test name)
  add_executable(${name} ${name}.cpp
    ../src/core/ModList.cpp
    ../src/core/PluginList.cpp
    ../src/core/Profile.cpp
    ../src/core/ProfileManager.cpp
    ../src/ai/AITransaction.cpp
  )
  target_include_directories(${name} PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
  target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Test)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_solero_test(test_ModList)
add_solero_test(test_VersionUtil)
add_solero_test(test_PluginList)
add_solero_test(test_Profile)

function(add_solero_deploy_test name)
  add_executable(${name} ${name}.cpp
    ../src/deploy/Linker.cpp
    ../src/deploy/DeployRecord.cpp
    ../src/deploy/ConflictIndex.cpp
    ../src/deploy/DeployEngine.cpp
    ../src/loot/LootSorter.cpp
    ../src/install/PluginScanner.cpp
    ../src/core/ModList.cpp
    ../src/core/PluginList.cpp
    ../src/core/Profile.cpp
    ../src/core/ProfileManager.cpp
    ../src/ai/AITransaction.cpp
    ../src/core/AppConfig.cpp
  )
  target_include_directories(${name} PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
  target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Test libloot::libloot)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_solero_deploy_test(test_Linker)
add_solero_deploy_test(test_DeployRecord)
add_solero_deploy_test(test_ConflictIndex)
add_solero_deploy_test(test_DeployEngine)

function(add_solero_install_test name)
  add_executable(${name} ${name}.cpp
    ../src/install/ArchiveTool.cpp
    ../src/install/DataDirDetector.cpp
    ../src/install/ModInstaller.cpp
    ../src/core/ModList.cpp
    ../src/core/PluginList.cpp
    ../src/core/Profile.cpp
    ../src/core/ProfileManager.cpp
    ../src/ai/AITransaction.cpp
    ../src/core/AppConfig.cpp
  )
  target_include_directories(${name} PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
  target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Test)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_solero_install_test(test_DataDirDetector)
add_solero_install_test(test_ModInstaller)

function(add_solero_fomod_test name)
  add_executable(${name} ${name}.cpp
    ../src/fomod/FomodCondition.cpp
    ../src/fomod/FomodEngine.cpp
  )
  target_include_directories(${name} PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
  target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Xml Qt6::Test)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_solero_fomod_test(test_FomodCondition)
add_solero_fomod_test(test_FomodEngine)

function(add_solero_tools_test name)
  add_executable(${name} ${name}.cpp
    ../src/tools/ToolStore.cpp
    ../src/install/DependencyChecker.cpp
    ../src/core/AppConfig.cpp
    ../src/core/ModList.cpp ../src/core/PluginList.cpp
    ../src/core/Profile.cpp ../src/core/ProfileManager.cpp ../src/ai/AITransaction.cpp
  )
  target_include_directories(${name} PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
  target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Test)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_solero_tools_test(test_ToolStore)
add_solero_tools_test(test_DependencyChecker)

function(add_solero_import_test name)
  add_executable(${name} ${name}.cpp
    ../src/import/Mo2Importer.cpp
    ../src/core/ModList.cpp ../src/core/PluginList.cpp
    ../src/core/Profile.cpp ../src/core/ProfileManager.cpp
    ../src/ai/AITransaction.cpp ../src/core/AppConfig.cpp
  )
  target_include_directories(${name} PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
  target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Test)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_solero_import_test(test_Mo2Importer)

add_executable(test_PluginScanner test_PluginScanner.cpp
  ../src/install/PluginScanner.cpp
  ../src/core/ModList.cpp ../src/core/PluginList.cpp)
target_include_directories(test_PluginScanner PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(test_PluginScanner PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_PluginScanner COMMAND test_PluginScanner)

add_executable(test_ToolCatalog test_ToolCatalog.cpp ../src/tools/ToolCatalog.cpp)
target_include_directories(test_ToolCatalog PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(test_ToolCatalog PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_ToolCatalog COMMAND test_ToolCatalog)

# Derive the Qt plugin dir (shared by headless tests below) from the actual
# Qt6::Core library location; fall back to qmake. See test_ModListModel notes.
if(NOT _qt_plugins)
  get_target_property(_qtcore_lib Qt6::Core IMPORTED_LOCATION_RELEASE)
  if(NOT _qtcore_lib)
    get_target_property(_qtcore_lib Qt6::Core IMPORTED_LOCATION)
  endif()
  get_filename_component(_qt_libdir "${_qtcore_lib}" DIRECTORY)
  set(_qt_plugins "${_qt_libdir}/../share/qt/plugins")
  if(NOT EXISTS "${_qt_plugins}/platforms")
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    execute_process(COMMAND "${_qmake_exe}" -query QT_INSTALL_PLUGINS
      OUTPUT_VARIABLE _qt_plugins OUTPUT_STRIP_TRAILING_WHITESPACE)
  endif()
endif()

add_executable(test_WabbajackEngine test_WabbajackEngine.cpp
  ../src/wabbajack/WabbajackEngine.cpp
  ../src/tools/ToolDownloader.cpp
  ../src/tools/ToolCatalog.cpp
  ../src/install/ArchiveTool.cpp
  ../src/core/AppConfig.cpp
  ../src/core/ModList.cpp ../src/core/PluginList.cpp
  ../src/core/Profile.cpp ../src/core/ProfileManager.cpp
  ../src/ai/AITransaction.cpp)
target_include_directories(test_WabbajackEngine PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(test_WabbajackEngine PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_WabbajackEngine COMMAND test_WabbajackEngine)
# Linking Qt6::Gui (QImage in ToolDownloader) makes QtTest spin up a
# QGuiApplication, which needs a platform plugin; run headless like ModListModel.
set_tests_properties(test_WabbajackEngine PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${_qt_plugins}")

add_executable(test_IniFile test_IniFile.cpp ../src/bethini/IniFile.cpp)
target_include_directories(test_IniFile PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(test_IniFile PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_IniFile COMMAND test_IniFile)

# ModListModel pulls in Qt Widgets/Svg (IconUtil) and AppConfig for its data()
# disk scans; the drag-drop tests only exercise moveRows/dropMimeData but still
# link the full unit.
add_executable(test_ModListModel test_ModListModel.cpp
  ../src/ui/ModListModel.cpp
  ../src/deploy/ConflictIndex.cpp
  ../src/core/AppConfig.cpp
  ../src/core/ModList.cpp ../src/core/PluginList.cpp
  ../src/core/Profile.cpp ../src/core/ProfileManager.cpp
  ../src/ai/AITransaction.cpp)
target_include_directories(test_ModListModel PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(test_ModListModel PRIVATE Qt6::Core Qt6::Widgets Qt6::Svg Qt6::Test)
add_test(NAME test_ModListModel COMMAND test_ModListModel)
# Widgets linkage means QApplication needs a platform plugin; run headless and
# point Qt at the real plugin dir. qmake's QT_INSTALL_PLUGINS can report a stale
# symlinked prefix, so derive the dir from the actual Qt6::Core library location
# (…/lib/libQt6Core.so → …/share/qt/plugins) and fall back to qmake.
get_target_property(_qtcore_lib Qt6::Core IMPORTED_LOCATION_RELEASE)
if(NOT _qtcore_lib)
  get_target_property(_qtcore_lib Qt6::Core IMPORTED_LOCATION)
endif()
get_filename_component(_qt_libdir "${_qtcore_lib}" DIRECTORY)
set(_qt_plugins "${_qt_libdir}/../share/qt/plugins")
if(NOT EXISTS "${_qt_plugins}/platforms")
  get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
  execute_process(COMMAND "${_qmake_exe}" -query QT_INSTALL_PLUGINS
    OUTPUT_VARIABLE _qt_plugins OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
set_tests_properties(test_ModListModel PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${_qt_plugins}")
