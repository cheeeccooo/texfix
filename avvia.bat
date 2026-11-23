@echo off
rem Ricompila se serve e apre Texfix (con la cartella di un progetto, se la dai).
setlocal
set "TEXFIX_HOME=%~dp0"
if not exist "%TEXFIX_HOME%build\CMakeCache.txt" (
  echo Prima lancia install.bat
  pause
  exit /b 1
)
for /f "delims=" %%q in ('dir /b /ad /o-n "%TEXFIX_HOME%.strumenti\qt\*" 2^>nul') do if not defined QTDIR if exist "%TEXFIX_HOME%.strumenti\qt\%%q\msvc2022_64\bin" set "QTDIR=%TEXFIX_HOME%.strumenti\qt\%%q\msvc2022_64"
if defined TEXFIX_QT set "QTDIR=%TEXFIX_QT%"
if defined QTDIR set "PATH=%QTDIR%\bin;%PATH%"
rem CMake fuori dal PATH? Usa quella che arriva con Visual Studio
where cmake >nul 2>nul || for /f "delims=" %%v in ('"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath 2^>nul') do set "PATH=%%v\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
cmake --build "%TEXFIX_HOME%build" --config Release >nul
start "" "%TEXFIX_HOME%build\Release\Texfix.exe" %*
