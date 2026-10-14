cmake_minimum_required(VERSION 3.20)
project(Displon LANGUAGES C CXX)
if(WIN32)
  enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

option(FT_BUILD_TESTS "Build the unit tests" ON)

add_compile_options(-Wall -Wextra)
if(WIN32)
  add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
  add_link_options(-static)
endif()

add_library(sqlite3 STATIC third_party/sqlite3/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite3)
target_compile_definitions(sqlite3 PRIVATE SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0 SQLITE_THREADSAFE=1)
target_compile_options(sqlite3 PRIVATE -w)
if(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(sqlite3 PUBLIC Threads::Threads)
endif()

add_library(ft_core STATIC
    common/BootTime.cpp
    common/Guid.cpp
    common/Random.cpp
    common/Sha256.cpp
    common/Utf.cpp
    common/PathUtil.cpp
    engine/BlockRule.cpp
    engine/LockState.cpp
    engine/BlockSession.cpp
    engine/EngineCore.cpp
    engine/ProcessMatcher.cpp
    engine/ProcessEnforcer.cpp
    engine/RuleWatcher.cpp
    engine/ProcessWatcher.cpp
    engine/WindowMatcher.cpp
    engine/WindowEnforcer.cpp
    engine/WindowTitleWatcher.cpp
    engine/HostsManager.cpp
    engine/NetworkFilter.cpp
    engine/Schedule.cpp
    engine/Scheduler.cpp
    engine/EngineHost.cpp
    engine/FileLockStateStore.cpp
    engine/ServiceGuard.cpp
    ipc/Protocol.cpp
    ipc/Dispatcher.cpp
    data/ConfigStore.cpp)
target_include_directories(ft_core PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(ft_core PUBLIC sqlite3)
if(WIN32)
  target_sources(ft_core PRIVATE
      engine/Win32ProcessOps.cpp
      engine/Win32WindowOps.cpp
      engine/TaskManagerPolicy.cpp
      engine/Win32HostsFile.cpp
      engine/Win32Network.cpp
      engine/Win32TimeZone.cpp
      ipc/PipeServer.cpp
      ipc/PipeClient.cpp)
  target_link_libraries(ft_core PUBLIC user32 dwmapi advapi32 fwpuclnt ws2_32)
else()
  target_link_libraries(ft_core PUBLIC Threads::Threads)
endif()

if(FT_BUILD_TESTS)
  enable_testing()
  add_executable(DisplonTests
      tests/test_main.cpp
      tests/test_process.cpp
      tests/test_window.cpp
      tests/test_hosts.cpp
      tests/test_network.cpp
      tests/test_schedule.cpp
      tests/test_host.cpp
      tests/test_ipc.cpp
      tests/test_lockstore.cpp
      tests/test_dispatcher.cpp)
  target_link_libraries(DisplonTests PRIVATE ft_core)
  add_test(NAME DisplonTests COMMAND DisplonTests)
endif()

if(WIN32)
  add_library(glad STATIC third_party/glad/src/gl.c)
  target_include_directories(glad PUBLIC third_party/glad/include)

  add_executable(DisplonUI WIN32
      ui/main.cpp
      ui/AppWindow.cpp
      ui/WizardApp.cpp
      ui/Win32Dialogs.cpp
      ui/SessionClient.cpp
      ui/LocalEngine.cpp
      ui/Notify.cpp
      ui/Autostart.cpp
      ui/SplashScreen.cpp
      ui/SystemTheme.cpp
      ui/Controls/CarbonStepper.cpp
      ui/gl/GLContext.cpp
      ui/gl/Renderer.cpp
      ui/gl/TextRenderer.cpp
      ui/gl/IconAtlas.cpp
      ui/Controls/CarbonButton.cpp
      ui/Controls/CarbonCheckbox.cpp
      ui/Controls/CarbonSegmented.cpp
      ui/DisplonUI.rc)
  target_include_directories(DisplonUI PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_options(DisplonUI PRIVATE -municode)
  target_link_libraries(DisplonUI PRIVATE glad opengl32 gdi32 user32 dwmapi ole32 oleaut32 shell32 uuid ft_core)
  # Bundle IBM Plex Sans + icons next to the UI binary
  add_custom_command(TARGET DisplonUI POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:DisplonUI>/assets/fonts"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:DisplonUI>/assets/icons"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets/fonts"
            "$<TARGET_FILE_DIR:DisplonUI>/assets/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets/icons"
            "$<TARGET_FILE_DIR:DisplonUI>/assets/icons"
    COMMENT "Copying fonts and Bootstrap Icons")

  add_executable(DisplonEngineHost tools/engine_host.cpp)
  target_link_options(DisplonEngineHost PRIVATE -municode)
  target_link_libraries(DisplonEngineHost PRIVATE ft_core)

  add_executable(DisplonSmoke tools/smoke_test.cpp)
  target_link_libraries(DisplonSmoke PRIVATE advapi32 fwpuclnt tdh dwmapi user32 ole32 shell32)

  add_executable(DisplonService service/ServiceMain.cpp)
  target_link_options(DisplonService PRIVATE -municode)
  target_link_libraries(DisplonService PRIVATE ft_core advapi32)

  add_executable(DisplonWatchdog watchdog/Watchdog.cpp)
  target_link_options(DisplonWatchdog PRIVATE -municode)
  target_link_libraries(DisplonWatchdog PRIVATE advapi32)
endif()
