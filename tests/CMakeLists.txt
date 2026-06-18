function(add_solero_test name)
  add_executable(${name} ${name}.cpp
    ../src/core/ModList.cpp
    ../src/core/PluginList.cpp
    ../src/core/LoadOrderBackup.cpp
    ../src/core/Profile.cpp
    ../src/core/ProfileManager.cpp
    ../src/core/AppConfig.cpp
    ../src/core/StagingFolder.cpp
    ../src/ai/AITransaction.cpp
  )
  target_include_directories(${name} PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
  target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Test)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_solero_test(test_ModList)
add_solero_test(test_AppConfig)
add_solero_test(test_VersionUtil)
add_solero_test(test_PluginList)
add_solero_test(test_Profile)
add_solero_test(test_LoadOrderBackup)

add_executable(test_MirrorPick test_MirrorPick.cpp ../src/nexus/MirrorPick.cpp)
target_include_directories(test_MirrorPick PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(test_MirrorPick PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_MirrorPick COMMAND test_MirrorPick)

# HealthCheck aggregates Profile + ConflictIndex data, so link the core profile
# set plus ConflictIndex and HealthCheck itself.
add_executable(test_HealthCheck test_HealthCheck.cpp
  ../src/core/HealthCheck.cpp
  ../src/deploy/ConflictIndex.cpp
  ../src/core/ModList.cpp
  ../src/core/PluginList.cpp
  ../src/core/LoadOrderBackup.cpp
  ../src/core/Profile.cpp
  ../src/core/ProfileManager.cpp
  ../src/core/AppConfig.cpp
  ../src/core/StagingFolder.cpp
  ../src/ai/AITransaction.cpp
)
target_include_directories(test_HealthCheck PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(test_HealthCheck PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_HealthCheck COMMAND test_HealthCheck)

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
    ../src/core/StagingFolder.cpp
  )
  target_include_directories(${name} PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
  target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Test libloot::libloot)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_solero_deploy_test(test_Linker)
add_solero_deploy_test(test_DeployRecord)
add_solero_deploy_test(test_ConflictIndex)
add_solero_deploy_test(test_DeployEngine)

# ToolRunner's capture helper depends on the deploy record/engine, so reuse the
# deploy source/link set plus ToolRunner.cpp itself.
add_executable(test_ToolRunner test_ToolRunner.cpp
  ../src/tools/ToolRunner.cpp
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
  ../src/core/StagingFolder.cpp
)
target_include_directories(test_ToolRunner PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(test_ToolRunner PRIVATE Qt6::Core Qt6::Test libloot::libloot)
add_test(NAME test_ToolRunner COMMAND test_ToolRunner)

# RadiumPrep reads a Profile's plugin list and writes a fake-mo2 tree +
# settings.json. Compile it with the core model sources (same set the simple
# add_solero_test helper uses) so Profile/PluginList resolve.
add_executable(test_RadiumPrep test_RadiumPrep.cpp
  ../src/tools/RadiumPrep.cpp
  ../src/core/ModList.cpp
  ../src/core/PluginList.cpp
  ../src/core/LoadOrderBackup.cpp
  ../src/core/Profile.cpp
  ../src/core/ProfileManager.cpp
  ../src/core/AppConfig.cpp
  ../src/core/StagingFolder.cpp
  ../src/ai/AITransaction.cpp
)
target_include_directories(test_RadiumPrep PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(test_RadiumPrep PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_RadiumPrep COMMAND test_RadiumPrep)

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
    ../src/core/StagingFolder.cpp
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

# FomodScanner: the test exercises only the pure classifyModule()/
# reconstructSelection(), but the translation unit also links scanProfile()'s IO
# deps (ArchiveLocator/ArchiveTool/ModInstaller/Profile), so link the same set
# PatchScanner uses plus the archive locator + scanner.
add_executable(test_FomodScanner test_FomodScanner.cpp
  ../src/fomod/FomodScanner.cpp
  ../src/fomod/FomodCondition.cpp
  ../src/fomod/FomodEngine.cpp
  ../src/install/ArchiveLocator.cpp
  ../src/install/ArchiveTool.cpp
  ../src/install/DataDirDetector.cpp
  ../src/install/ModInstaller.cpp
  ../src/install/PluginScanner.cpp
  ../src/core/ModList.cpp
  ../src/core/PluginList.cpp
  ../src/core/Profile.cpp
  ../src/core/ProfileManager.cpp
  ../src/ai/AITransaction.cpp
  ../src/core/AppConfig.cpp
  ../src/core/StagingFolder.cpp
)
target_include_directories(test_FomodScanner PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(test_FomodScanner PRIVATE Qt6::Core Qt6::Xml Qt6::Test)
add_test(NAME test_FomodScanner COMMAND test_FomodScanner)

# PatchScanner: the test exercises only the pure candidatesForModule(), but the
# translation unit also pulls in scanProfile()'s IO deps (ModInstaller/ArchiveTool/
# Profile), so link the same set the install test uses.
add_executable(test_PatchScanner test_PatchScanner.cpp
  ../src/patch/PatchScanner.cpp
  ../src/fomod/FomodCondition.cpp
  ../src/fomod/FomodEngine.cpp
  ../src/install/ArchiveTool.cpp
  ../src/install/DataDirDetector.cpp
  ../src/install/ModInstaller.cpp
  ../src/install/PluginScanner.cpp
  ../src/core/ModList.cpp
  ../src/core/PluginList.cpp
  ../src/core/Profile.cpp
  ../src/core/ProfileManager.cpp
  ../src/ai/AITransaction.cpp
  ../src/core/AppConfig.cpp
  ../src/core/StagingFolder.cpp
)
target_include_directories(test_PatchScanner PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(test_PatchScanner PRIVATE Qt6::Core Qt6::Xml Qt6::Test)
add_test(NAME test_PatchScanner COMMAND test_PatchScanner)

function(add_solero_tools_test name)
  add_executable(${name} ${name}.cpp
    ../src/tools/ToolStore.cpp
    ../src/install/DependencyChecker.cpp
    ../src/core/AppConfig.cpp
    ../src/core/StagingFolder.cpp
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
    ../src/core/StagingFolder.cpp
  )
  target_include_directories(${name} PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
  target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Test)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_solero_import_test(test_Mo2Importer)

add_executable(test_ProfileManifest test_ProfileManifest.cpp
  ../src/io/ProfileManifest.cpp
  ../src/core/ModList.cpp ../src/core/PluginList.cpp
  ../src/core/Profile.cpp ../src/core/ProfileManager.cpp
  ../src/core/StagingFolder.cpp ../src/core/AppConfig.cpp
  ../src/ai/AITransaction.cpp)
target_include_directories(test_ProfileManifest PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(test_ProfileManifest PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_ProfileManifest COMMAND test_ProfileManifest)

add_executable(test_PluginScanner test_PluginScanner.cpp
  ../src/install/PluginScanner.cpp
  ../src/core/ModList.cpp ../src/core/PluginList.cpp ../src/core/StagingFolder.cpp)
target_include_directories(test_PluginScanner PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(test_PluginScanner PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_PluginScanner COMMAND test_PluginScanner)

add_executable(test_ToolCatalog test_ToolCatalog.cpp ../src/tools/ToolCatalog.cpp)
target_include_directories(test_ToolCatalog PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(test_ToolCatalog PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_ToolCatalog COMMAND test_ToolCatalog)

# ProtonRuntime is a header-only pure classifier (off-site requirement → runtime
# name), so the test needs no extra sources.
add_executable(test_ProtonRuntime test_ProtonRuntime.cpp)
target_include_directories(test_ProtonRuntime PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(test_ProtonRuntime PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_ProtonRuntime COMMAND test_ProtonRuntime)

add_executable(test_ToolNameMap test_ToolNameMap.cpp ../src/tools/ToolNameMap.cpp)
target_include_directories(test_ToolNameMap PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(test_ToolNameMap PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_ToolNameMap COMMAND test_ToolNameMap)

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
  ../src/core/StagingFolder.cpp
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

# WabbajackDialog filter predicate (pure static helpers passesFilters/collectTags).
# The TU pulls in the full dialog + Mo2Importer + NexusApi + ToolDownloader chain,
# so link that set; run headless like the other Widgets-linked tests.
add_executable(test_WabbajackFilter test_WabbajackFilter.cpp
  ../src/ui/WabbajackDialog.cpp
  ../src/wabbajack/WabbajackEngine.cpp
  ../src/import/Mo2Importer.cpp
  ../src/nexus/NexusApi.cpp
  ../src/nexus/MirrorPick.cpp
  ../src/tools/ToolDownloader.cpp
  ../src/tools/ToolCatalog.cpp
  ../src/install/ArchiveTool.cpp
  ../src/core/AppConfig.cpp
  ../src/core/StagingFolder.cpp
  ../src/core/ModList.cpp ../src/core/PluginList.cpp
  ../src/core/Profile.cpp ../src/core/ProfileManager.cpp
  ../src/ai/AITransaction.cpp)
target_include_directories(test_WabbajackFilter PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(test_WabbajackFilter PRIVATE
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test)
add_test(NAME test_WabbajackFilter COMMAND test_WabbajackFilter)
set_tests_properties(test_WabbajackFilter PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${_qt_plugins}")

# NxmHandler: the test exercises pure parse()/isSupportedGame(), but the TU links
# NexusApi + ToolDownloader (curl/QImage), so include that set. Headless for QImage.
add_executable(test_NxmHandler test_NxmHandler.cpp
  ../src/nexus/NxmHandler.cpp
  ../src/nexus/NexusApi.cpp
  ../src/nexus/MirrorPick.cpp
  ../src/tools/ToolDownloader.cpp
  ../src/tools/ToolCatalog.cpp
  ../src/install/ArchiveTool.cpp
  ../src/core/AppConfig.cpp
  ../src/core/StagingFolder.cpp
  ../src/core/ModList.cpp ../src/core/PluginList.cpp
  ../src/core/Profile.cpp ../src/core/ProfileManager.cpp
  ../src/ai/AITransaction.cpp)
target_include_directories(test_NxmHandler PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(test_NxmHandler PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Test)
add_test(NAME test_NxmHandler COMMAND test_NxmHandler)
set_tests_properties(test_NxmHandler PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${_qt_plugins}")

add_executable(test_IniFile test_IniFile.cpp ../src/bethini/IniFile.cpp)
target_include_directories(test_IniFile PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(test_IniFile PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_IniFile COMMAND test_IniFile)

add_executable(test_FileMove test_FileMove.cpp ../src/core/FileMove.cpp)
target_include_directories(test_FileMove PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(test_FileMove PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_FileMove COMMAND test_FileMove)

add_executable(test_StagingFolder test_StagingFolder.cpp ../src/core/StagingFolder.cpp)
target_include_directories(test_StagingFolder PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(test_StagingFolder PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_StagingFolder COMMAND test_StagingFolder)

add_executable(test_OutputModMigration test_OutputModMigration.cpp
  ../src/core/OutputModMigration.cpp
  ../src/core/StagingFolder.cpp
  ../src/core/ModList.cpp)
target_include_directories(test_OutputModMigration PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(test_OutputModMigration PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_OutputModMigration COMMAND test_OutputModMigration)

add_executable(test_ShaderCache test_ShaderCache.cpp
  ../src/core/ShaderCache.cpp
  ../src/core/ModList.cpp)
target_include_directories(test_ShaderCache PRIVATE ../src ${CMAKE_BINARY_DIR}/include)
target_link_libraries(test_ShaderCache PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_ShaderCache COMMAND test_ShaderCache)

# ModListModel pulls in Qt Widgets/Svg (IconUtil) and AppConfig for its data()
# disk scans; the drag-drop tests only exercise moveRows/dropMimeData but still
# link the full unit.
add_executable(test_ModListModel test_ModListModel.cpp
  ../src/ui/ModListModel.cpp
  ../src/deploy/ConflictIndex.cpp
  ../src/core/AppConfig.cpp
  ../src/core/StagingFolder.cpp
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
