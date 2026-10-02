add_library(Briefcase.DeceiveInc.Static STATIC
  src/Hooks.cpp
  src/MatchState.cpp
  src/LootDistribution.cpp
  src/NativeStamina.cpp
  src/NativeStaminaSites.cpp
  src/Paths.cpp
  src/Spy.cpp)

target_include_directories(Briefcase.DeceiveInc.Static PUBLIC include)
target_link_libraries(Briefcase.DeceiveInc.Static PUBLIC UE4SS PRIVATE PolyHook_2 Zydis)
set_target_properties(Briefcase.DeceiveInc.Static PROPERTIES
  POSITION_INDEPENDENT_CODE ON)

add_library(Briefcase::DeceiveInc ALIAS Briefcase.DeceiveInc.Static)

if(BUILD_TESTING)
  add_executable(Briefcase.DeceiveInc.Contracts tests/Contracts.cpp)
  target_include_directories(Briefcase.DeceiveInc.Contracts PRIVATE include)
  add_test(NAME Briefcase.DeceiveInc.Contracts COMMAND Briefcase.DeceiveInc.Contracts)
  if(WIN32)
    add_executable(Briefcase.NativeStamina.Sites tests/NativeStaminaSites.cpp src/NativeStaminaSites.cpp)
    target_link_libraries(Briefcase.NativeStamina.Sites PRIVATE Zydis)
    add_test(NAME Briefcase.NativeStamina.Sites COMMAND Briefcase.NativeStamina.Sites)
  endif()
endif()
