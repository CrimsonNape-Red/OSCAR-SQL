setlocal
:::@echo off
::: Build the 64-bit version of OSCAR for Windows with MSVC, then the installer and the
::: portable ZIP (deploy.bat). Qt Bluetooth works on Windows only in MSVC builds, so
::: this is the build that has Bluetooth downloads in the Oximetry Wizard (MinGW builds
::: have the USB cables but no Bluetooth).
:::
::: Usage: buildall-msvc.bat [qtVersion] [qtPath] [installerSuffix]
:::     qtVersion  defaults to 6.10.2
:::     qtPath     defaults to C:\Qt   (the folder that holds <qtVersion>\msvc2022_64)
:::     installerSuffix is appended to the installer and ZIP names, e.g. -BLE
:::
::: Requirements:
:::     Visual Studio 2022 or its Build Tools, with the "Desktop development with C++" workload
:::     Qt <qtVersion> MSVC 2022 64-bit, with Qt 5 Compatibility, Qt Serial Port and Qt Connectivity
:::     Git for Windows, Inno Setup 6 (see deploy.bat)
set qtVersion=%1
if "%1" == "" set qtVersion=6.10.2
set qtPath=%2
if "%2" == "" set qtPath=C:\Qt
set suffix=%3

set QTDIR=%qtPath%\%qtVersion%\msvc2022_64
if NOT exist %QTDIR%\bin\qmake.exe (
	echo *** Qt MSVC kit not found: %QTDIR%
	goto :makefail
	)
echo QTDIR is %QTDIR%

::: Find Visual Studio and set up its 64-bit compiler environment.
set vswhere="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`%vswhere% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set vsPath=%%i
if "%vsPath%" == "" (
	echo *** Visual Studio with the C++ tools not found
	goto :makefail
	)
call "%vsPath%\VC\Auxiliary\Build\vcvars64.bat" >nul || goto :makefail

set PATH=%QTDIR%\bin;%PATH%
::: Compile several files at once in each cl call (nmake itself runs one job at a time).
set CL=/MP

set savedir=%cd%

: Construct name of our build directory
set dirname=build-oscar-win_64_bit
echo Build directory is %dirname%

set basedir=%~dp0..\..
if exist %basedir%\%dirname%\nul rmdir /s /q %basedir%\%dirname%
mkdir %basedir%\%dirname%
cd %basedir%\%dirname%

qmake.exe ..\oscar\oscar.pro -spec win32-msvc CONFIG+=release >qmake.log 2>&1 || goto :makefail
nmake /nologo >make.log 2>&1 || goto :makefail

call ..\Building\Windows\deploy.bat %suffix% || goto :makefail

echo === MAKE  SUCCESSFUL ===
cd %savedir%
endlocal
exit /b

:makefail
cd %savedir%
endlocal
echo *** MAKE  FAILED ***
exit /b 1
