# Development-only integration of the separately built, pinned headless Unreal
# libraries. UE4SSProgram, its Lua loader and its UI are never linked here.
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19)
  message(FATAL_ERROR "The experimental Linux backend requires Clang 19 or later")
endif()
set(BRIEFCASE_UE4SS_LINUX_SOURCE "" CACHE PATH "Pinned Linux UE4SS source checkout")
set(BRIEFCASE_UE4SS_LINUX_BUILD "" CACHE PATH "Matching Clang 19 UE4SS build")
set(BRIEFCASE_UE4SS_LINUX_DEPS "" CACHE PATH "UE4SS FetchContent dependency directory")
find_package(Git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${BRIEFCASE_UE4SS_LINUX_SOURCE}" rev-parse HEAD
  OUTPUT_VARIABLE backend_revision OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE revision_status)
if(NOT revision_status EQUAL 0 OR NOT backend_revision STREQUAL "7894d53f6e13011a16445f28e6f7cd46d58c72cc")
  message(FATAL_ERROR "Expected the reviewed Linux UE4SS revision; see docs/LinuxServer.md")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${BRIEFCASE_UE4SS_LINUX_SOURCE}/deps/first/patternsleuth" rev-parse HEAD
  OUTPUT_VARIABLE scanner_revision OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT scanner_revision STREQUAL "23d13d7471c854fb15b586deb2f2678a1b7bc690")
  message(FATAL_ERROR "Unexpected Linux scanner revision")
endif()
foreach(pair "${BRIEFCASE_UE4SS_LINUX_SOURCE};ue4ss-linux-time.patch"
             "${BRIEFCASE_UE4SS_LINUX_SOURCE}/deps/first/patternsleuth;ue4ss-linux-resolvers.patch")
  list(GET pair 0 patch_root)
  list(GET pair 1 patch_name)
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${patch_root}" apply --reverse --check
    "${CMAKE_SOURCE_DIR}/cmake/patches/${patch_name}" RESULT_VARIABLE patch_status)
  if(NOT patch_status EQUAL 0)
    message(FATAL_ERROR "Reviewed Linux patch missing: ${patch_name}")
  endif()
endforeach()
set(backend_lib "${BRIEFCASE_UE4SS_LINUX_BUILD}/Game__Shipping__Linux/lib")
set(backend_archives)
foreach(name Unreal patternsleuth_bind SinglePassSigScanner ASMHelper DynamicOutput File Helpers
             PolyHook_2 Zydis Zycore asmtk asmjit fmt)
  if(NOT EXISTS "${backend_lib}/lib${name}.a")
    message(FATAL_ERROR "Missing headless backend archive: lib${name}.a")
  endif()
  list(APPEND backend_archives "${backend_lib}/lib${name}.a")
endforeach()
add_library(Briefcase.NativeHost SHARED runtime/Briefcase.NativeHost/Host.cpp
  runtime/Briefcase.UnrealBackend/Backend.cpp)
target_compile_definitions(Briefcase.NativeHost PRIVATE BRIEFCASE_FRAMEWORK_VERSION="${PROJECT_VERSION}")
target_include_directories(Briefcase.NativeHost PRIVATE runtime/Briefcase.UnrealBackend)
foreach(name Unreal File Helpers DynamicOutput SinglePassSigScanner Constructs Function ASMHelper String Profiler)
  target_include_directories(Briefcase.NativeHost SYSTEM PRIVATE
    "${BRIEFCASE_UE4SS_LINUX_SOURCE}/deps/first/${name}/include")
endforeach()
target_include_directories(Briefcase.NativeHost SYSTEM PRIVATE
  "${BRIEFCASE_UE4SS_LINUX_SOURCE}/deps/first/Unreal/generated_include"
  "${BRIEFCASE_UE4SS_LINUX_SOURCE}/deps/first/Unreal/include/Unreal"
  "${BRIEFCASE_UE4SS_LINUX_SOURCE}/deps/first/Unreal/include/Unreal/Core"
  "${BRIEFCASE_UE4SS_LINUX_DEPS}/fmt-src/include"
  "${BRIEFCASE_UE4SS_LINUX_DEPS}/polyhook2-src"
  "${BRIEFCASE_UE4SS_LINUX_DEPS}/polyhook2-src/asmjit/src"
  "${BRIEFCASE_UE4SS_LINUX_DEPS}/zydis-src/include"
  "${BRIEFCASE_UE4SS_LINUX_DEPS}/zydis-build"
  "${BRIEFCASE_UE4SS_LINUX_DEPS}/zydis-src/dependencies/zycore/include"
  "${BRIEFCASE_UE4SS_LINUX_DEPS}/zydis-build/zycore")
target_compile_definitions(Briefcase.NativeHost PRIVATE PLATFORM_LINUX PLATFORM_UNIX LINUX
  UBT_COMPILED_PLATFORM=Linux OVERRIDE_PLATFORM_HEADER_NAME=Linux UE_BUILD_SHIPPING UE_GAME
  RC_UNREAL_BUILD_STATIC RC_DYNAMIC_OUTPUT_BUILD_STATIC RC_FILE_BUILD_STATIC RC_STRING_BUILD_STATIC
  RC_ASM_HELPER_BUILD_STATIC RC_SINGLE_PASS_SIG_SCANNER_BUILD_STATIC ZYDIS_STATIC_BUILD ZYCORE_STATIC_BUILD)
target_compile_options(Briefcase.NativeHost PRIVATE -fms-extensions -Wno-ignored-attributes)
execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=libstdc++.a
  OUTPUT_VARIABLE cpp_runtime OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT EXISTS "${cpp_runtime}")
  message(FATAL_ERROR "Static C++ runtime required for allocator isolation")
endif()
find_program(BRIEFCASE_LLD NAMES ld.lld-19 REQUIRED)
target_link_options(Briefcase.NativeHost PRIVATE "-fuse-ld=${BRIEFCASE_LLD}"
  -Wl,-Bsymbolic -Wl,--exclude-libs,ALL -Wl,-z,defs -static-libstdc++ -static-libgcc)
target_link_libraries(Briefcase.NativeHost PRIVATE Briefcase.Core Briefcase.Admin.Service
  -Wl,--whole-archive "${cpp_runtime}" -Wl,--no-whole-archive
  -Wl,--start-group ${backend_archives} -Wl,--end-group Threads::Threads ${CMAKE_DL_LIBS} m util rt)
set_target_properties(Briefcase.NativeHost PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
add_library(Briefcase.ServerBootstrap SHARED loader/Briefcase.ServerLauncher/LinuxBootstrap.cpp)
target_link_libraries(Briefcase.ServerBootstrap PRIVATE ${CMAKE_DL_LIBS})
set_target_properties(Briefcase.ServerBootstrap PROPERTIES CXX_VISIBILITY_PRESET hidden)
add_library(Briefcase.RuntimeProbe SHARED samples/Briefcase.RuntimeProbe/Probe.cpp)
target_link_libraries(Briefcase.RuntimeProbe PRIVATE Briefcase.ModApi)
set_target_properties(Briefcase.RuntimeProbe PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden)
add_library(Briefcase.LinuxHostFixture SHARED tests/LinuxHostFixture.cpp)
add_executable(Briefcase.LinuxGameFixture tests/LinuxGameFixture.cpp)
target_link_libraries(Briefcase.LinuxGameFixture PRIVATE Briefcase.Admin.Service)
add_test(NAME LinuxLauncherContracts COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_SOURCE_DIR}/tests/LinuxLauncherContracts.py" "${CMAKE_SOURCE_DIR}/scripts/linux/launch-server.sh"
  "$<TARGET_FILE:Briefcase.ServerBootstrap>" "$<TARGET_FILE:Briefcase.LinuxHostFixture>"
  "$<TARGET_FILE:Briefcase.LinuxGameFixture>")
set_tests_properties(LinuxLauncherContracts PROPERTIES TIMEOUT 45)
add_executable(Briefcase.LinuxDetourContracts tests/LinuxDetourContracts.cpp)
target_include_directories(Briefcase.LinuxDetourContracts SYSTEM PRIVATE
  "$<TARGET_PROPERTY:Briefcase.NativeHost,INCLUDE_DIRECTORIES>")
target_compile_definitions(Briefcase.LinuxDetourContracts PRIVATE ZYDIS_STATIC_BUILD ZYCORE_STATIC_BUILD)
target_link_libraries(Briefcase.LinuxDetourContracts PRIVATE
  -Wl,--start-group ${backend_archives} -Wl,--end-group Threads::Threads ${CMAKE_DL_LIBS})
add_test(NAME LinuxDetourContracts COMMAND Briefcase.LinuxDetourContracts)
