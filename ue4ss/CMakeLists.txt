cmake_minimum_required(VERSION 3.22)

project(BriefcaseUE4SS LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# UE4SS owns Unreal discovery and the C++ mod lifecycle in this build. Briefcase
# remains a game-specific static SDK linked into each mod.
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../third_party/RE-UE4SS"
                 "${CMAKE_CURRENT_BINARY_DIR}/RE-UE4SS")

include(FetchContent)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG 9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03)
FetchContent_MakeAvailable(nlohmann_json)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../sdk/Briefcase.DeceiveInc/ue4ss"
                 "${CMAKE_CURRENT_BINARY_DIR}/Briefcase.DeceiveInc")

# This framework-owned server bridge is separate from user gameplay mods. It
# streams authoritative local state to ServerApp through a Windows named pipe.
if(WIN32)
  add_executable(Briefcase.ServerEquipmentIds.Tests tests/ServerEquipmentIds.cpp)
  add_test(NAME ServerEquipmentIds COMMAND Briefcase.ServerEquipmentIds.Tests)
  add_library(Briefcase.ServerBridge.UE4SS SHARED ServerBridge.cpp)
  target_include_directories(Briefcase.ServerBridge.UE4SS PRIVATE
    $<TARGET_PROPERTY:UE4SS,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:Unreal,INTERFACE_INCLUDE_DIRECTORIES>)
  target_compile_definitions(Briefcase.ServerBridge.UE4SS PRIVATE
    $<TARGET_PROPERTY:UE4SS,INTERFACE_COMPILE_DEFINITIONS>
    $<TARGET_PROPERTY:Unreal,INTERFACE_COMPILE_DEFINITIONS>)
  target_link_libraries(Briefcase.ServerBridge.UE4SS PRIVATE
    "$<TARGET_LINKER_FILE:UE4SS>" nlohmann_json::nlohmann_json advapi32)
  add_dependencies(Briefcase.ServerBridge.UE4SS UE4SS)
  set_target_properties(Briefcase.ServerBridge.UE4SS PROPERTIES OUTPUT_NAME main)
endif()
# Independent mod repositories are optional local siblings. The framework
# release checks out none of them and contains no gameplay mod binaries.
option(BRIEFCASE_BUILD_LOCAL_MODS "Discover independent local mod repositories" ON)
if(BRIEFCASE_BUILD_LOCAL_MODS)
  file(GLOB mod_projects LIST_DIRECTORIES true
    "${CMAKE_CURRENT_LIST_DIR}/../repositories/*/ue4ss/CMakeLists.txt")
  foreach(mod_project IN LISTS mod_projects)
    get_filename_component(mod_source "${mod_project}" DIRECTORY)
    get_filename_component(mod_repository "${mod_source}/.." ABSOLUTE)
    get_filename_component(mod_name "${mod_repository}" NAME)
    add_subdirectory("${mod_source}" "${CMAKE_CURRENT_BINARY_DIR}/${mod_name}")
  endforeach()
endif()
