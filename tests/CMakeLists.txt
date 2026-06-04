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
