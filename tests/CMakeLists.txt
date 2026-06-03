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
