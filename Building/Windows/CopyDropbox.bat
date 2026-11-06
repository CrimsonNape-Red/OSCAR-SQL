@echo off
setlocal enabledelayedexpansion

::: Current Directory is the shadowBuildDir
set shadowBuildDir=%cd%

git fetch --tags
for /f "tokens=*" %%i in ('git describe --exact-match --tags 2^>nul') do set TAG=%%i

if defined TAG (
    set COPIED=0
    rem The installer and the portable ZIP
    for %%f in (%shadowBuildDir%\Installer\*.exe %shadowBuildDir%\Installer\*.zip) do (
        echo %%~nf | findstr /i "\-plus" >nul && (
            echo %%~nxf contains -plus so not copied to Dropbox
        ) || (
            copy "%%f" "C:\Users\Guy\Dropbox\OSCAR Files"
            set COPIED=1
        )
    )
    if !COPIED!==1 echo Copied installer and portable ZIP for: %TAG%
) else (
    echo Current commit is not tagged
)