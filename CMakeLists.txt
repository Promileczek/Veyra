cmake_minimum_required(VERSION 3.20)
project(Exploder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Statyczny CRT: brak zależności od vcruntime*.dll
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)

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
    src/shell/SystemStatus.cpp
    src/shell/ScreenSnip.cpp
    src/shell/SystemStats.cpp
    src/shell/AltTab.cpp
    src/shell/WindowDots.cpp
    src/taskbar/Taskbar.cpp
    src/taskbar/AppIcons.cpp
    src/taskbar/TrayFlyout.cpp
    src/taskbar/StartMenu.cpp
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
target_link_libraries(exploder PRIVATE d3d11 dxgi d2d1 dwrite dcomp windowscodecs dwmapi shell32 user32 ole32 dxguid windowsapp iphlpapi wlanapi version ws2_32 shlwapi shcore dbghelp powrprof)
# Symbole (PDB) + mapa: awarię (crash.dmp / dziennik zdarzeń) da się przypisać do linijki kodu.
# /OPT:REF /OPT:ICF jawnie — /DEBUG domyślnie je wyłącza, a nie chcemy większego exe.
target_compile_options(exploder PRIVATE /Zi)
target_link_options(exploder PRIVATE /DEBUG /OPT:REF /OPT:ICF /MAP)

# Aplikacje Explodera (osobne procesy — awaria jednej nie kładzie powłoki; RAM tylko gdy otwarte)
set(EXPLODER_APP_CORE src/core/Graphics.cpp src/core/Accent.cpp src/core/AppWindow.cpp src/core/TrafficLights.cpp src/core/Settings.cpp src/app.manifest)
foreach(app terminal calc store)
    if(app STREQUAL "terminal")
        set(src apps/terminal/Terminal.cpp)
    elseif(app STREQUAL "calc")
        set(src apps/calc/Calculator.cpp)
    else()
        set(src apps/store/Store.cpp)
    endif()
    add_executable(exploder-${app} WIN32 ${src} apps/${app}/${app}.rc ${EXPLODER_APP_CORE})
    target_compile_options(exploder-${app} PRIVATE /W4 /utf-8 /permissive- /Zi)
    target_link_libraries(exploder-${app} PRIVATE d3d11 dxgi d2d1 dwrite dcomp windowscodecs dwmapi user32 ole32 shell32 dxguid advapi32 urlmon)
    target_link_options(exploder-${app} PRIVATE /DEBUG /OPT:REF /OPT:ICF)
endforeach()
