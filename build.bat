@echo off
rem Buduje Exploder do %LOCALAPPDATA%\Exploder\build (dysk lokalny: X: to udzial sieciowy,
rem a powloka startuje przy logowaniu, zanim dysk sieciowy jest podpiety).
setlocal
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set VSDIR=%%i
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set OUT=%LOCALAPPDATA%\Exploder\build
cmake -S "%~dp0." -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build "%OUT%" || exit /b 1
echo.
echo Gotowe: %OUT%\exploder.exe
