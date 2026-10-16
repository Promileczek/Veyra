cmake_minimum_required(VERSION 3.20)
project(Exploder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Statyczny CRT: brak zależności od vcruntime*.dll
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
# Wersja Veyry: plik VERSION (jedno źródło — porównywany z GitHubem w Ustawieniach → Aktualizacje)
file(READ "${CMAKE_SOURCE_DIR}/VERSION" VEYRA_VERSION)
string(STRIP "${VEYRA_VERSION}" VEYRA_VERSION)
add_compile_definitions(VEYRA_VERSION="${VEYRA_VERSION}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/VERSION")

add_executable(exploder WIN32
    src/main.cpp
    src/core/Graphics.cpp
    src/core/Accent.cpp
    src/core/WorkArea.cpp
    src/core/Tooltip.cpp
    src/core/Popup.cpp
    src/core/Settings.cpp
    src/core/TrafficLights.cpp
    src/shell/WindowTracker.cpp
    src/shell/TrayService.cpp
    src/shell/MediaService.cpp
    src/shell/NowPlayingHost.cpp
    src/shell/WindowAnim.cpp
    src/shell/SystemStatus.cpp
    src/shell/ScreenSnip.cpp
    src/shell/SystemStats.cpp
    src/shell/AltTab.cpp
    src/shell/WindowDots.cpp
    src/shell/VeyraCursor.cpp
    src/shell/ExplorerUi.cpp
    src/shell/Notification.cpp
    src/taskbar/Taskbar.cpp
    src/taskbar/AppIcons.cpp
    src/taskbar/TrayFlyout.cpp
    src/taskbar/StartMenu.cpp
    src/taskbar/RunBox.cpp
    src/taskbar/ClipboardPanel.cpp
    src/desktop/Desktop.cpp
    src/desktop/DesktopItems.cpp
    src/desktop/AppLauncher.cpp
    src/desktop/AppPicker.cpp
    src/desktop/FilesWidget.cpp
    src/desktop/StatsWidget.cpp
    src/files/FileView.cpp
    src/files/FileWindow.cpp
    src/topbar/TopBar.cpp
    src/topbar/StatusPopups.cpp
    src/app.manifest
)

target_compile_options(exploder PRIVATE /W4 /utf-8 /permissive-)
target_link_libraries(exploder PRIVATE d3d11 dxgi d2d1 dwrite dcomp windowscodecs dwmapi shell32 user32 ole32 dxguid windowsapp iphlpapi wlanapi version ws2_32 shlwapi shcore dbghelp powrprof winhttp userenv)
# Symbole (PDB) + mapa: awarię (crash.dmp / dziennik zdarzeń) da się przypisać do linijki kodu.
# /OPT:REF /OPT:ICF jawnie — /DEBUG domyślnie je wyłącza, a nie chcemy większego exe.
target_compile_options(exploder PRIVATE /Zi)
target_link_options(exploder PRIVATE /DEBUG /OPT:REF /OPT:ICF /MAP)
# Tapety Veyry obok exploder.exe (domyślna ustawiana przy pierwszym starcie)
add_custom_command(TARGET exploder POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/res/wallpapers $<TARGET_FILE_DIR:exploder>/wallpapers)

# Aplikacje Explodera (osobne procesy — awaria jednej nie kładzie powłoki; RAM tylko gdy otwarte)
set(EXPLODER_APP_CORE src/core/Graphics.cpp src/core/Accent.cpp src/core/AppWindow.cpp src/core/TrafficLights.cpp src/core/Settings.cpp src/app.manifest)
foreach(app terminal calc store settings taskmgr)
    if(app STREQUAL "terminal")
        set(src apps/terminal/Terminal.cpp)
    elseif(app STREQUAL "calc")
        set(src apps/calc/Calculator.cpp)
    elseif(app STREQUAL "settings")
        set(src apps/settings/SettingsApp.cpp)
    elseif(app STREQUAL "taskmgr")
        set(src apps/taskmgr/TaskManager.cpp)
    else()
        set(src apps/store/Store.cpp)
    endif()
    add_executable(exploder-${app} WIN32 ${src} apps/${app}/${app}.rc ${EXPLODER_APP_CORE})
    target_compile_options(exploder-${app} PRIVATE /W4 /utf-8 /permissive- /Zi)
    target_link_libraries(exploder-${app} PRIVATE d3d11 dxgi d2d1 dwrite dcomp windowscodecs dwmapi user32 ole32 shell32 dxguid advapi32 shlwapi winhttp)
    if(app STREQUAL "taskmgr")
        target_link_libraries(exploder-${app} PRIVATE psapi iphlpapi)
    endif()
    target_link_options(exploder-${app} PRIVATE /DEBUG /OPT:REF /OPT:ICF)
endforeach()

# Instalator VeyraSetup.exe: programy i tapety w środku (RCDATA), wersje Minimal / Full, deinstalacja (/odinstaluj)
set(SETUP_PAYLOAD ${CMAKE_BINARY_DIR}/setup_payload.rc)
set(SETUP_FILES
    I_EXPLODER ${CMAKE_BINARY_DIR}/exploder.exe
    I_TERMINAL ${CMAKE_BINARY_DIR}/exploder-terminal.exe
    I_CALC ${CMAKE_BINARY_DIR}/exploder-calc.exe
    I_STORE ${CMAKE_BINARY_DIR}/exploder-store.exe
    I_TASKMGR ${CMAKE_BINARY_DIR}/exploder-taskmgr.exe
    I_SETTINGS ${CMAKE_BINARY_DIR}/exploder-settings.exe
    I_WP_DOMYSLNA ${CMAKE_SOURCE_DIR}/res/wallpapers/veyra-domyslna.jpg
    I_WP_JEZIORO ${CMAKE_SOURCE_DIR}/res/wallpapers/veyra-jezioro.jpg
    I_WP_GORY ${CMAKE_SOURCE_DIR}/res/wallpapers/veyra-gory.jpg
    I_WP_ZORZA ${CMAKE_SOURCE_DIR}/res/wallpapers/veyra-zorza.jpg
    I_ZADANIE ${CMAKE_SOURCE_DIR}/apps/setup/veyra-zadanie.ps1)
set(SETUP_RC "")
set(SETUP_DEPENDS "")
list(LENGTH SETUP_FILES n)
math(EXPR last "${n} - 1")
foreach(i RANGE 0 ${last} 2)
    math(EXPR j "${i} + 1")
    list(GET SETUP_FILES ${i} res)
    list(GET SETUP_FILES ${j} file)
    string(APPEND SETUP_RC "${res} RCDATA \"${file}\"\n")
    list(APPEND SETUP_DEPENDS ${file})
endforeach()
file(GENERATE OUTPUT ${SETUP_PAYLOAD} CONTENT "${SETUP_RC}")
set_source_files_properties(${SETUP_PAYLOAD} PROPERTIES GENERATED TRUE OBJECT_DEPENDS "${SETUP_DEPENDS}")
add_executable(veyra-setup WIN32 apps/setup/Setup.cpp apps/setup/setup.rc ${SETUP_PAYLOAD} ${EXPLODER_APP_CORE})
set_target_properties(veyra-setup PROPERTIES OUTPUT_NAME VeyraSetup)
add_dependencies(veyra-setup exploder exploder-terminal exploder-calc exploder-store exploder-taskmgr exploder-settings)
target_compile_options(veyra-setup PRIVATE /W4 /utf-8 /permissive-)
target_link_libraries(veyra-setup PRIVATE d3d11 dxgi d2d1 dwrite dcomp windowscodecs dwmapi user32 ole32 shell32 dxguid advapi32 shlwapi winhttp)
