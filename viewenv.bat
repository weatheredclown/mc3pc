@echo off
rem Shared by the view*.bat launchers (call "%~dp0viewenv.bat" || exit /b 1): sets RSCVIEW to
rem the viewer and RSCVIEW_ASSETS to the game data, then PATHARG to the matching -path switch.
rem The data can be the disc files as they come off the ISO (the folder holding ASSETS.DAT) or
rem a loose asset tree.  Looked for, in order:
rem   %RSCVIEW_ASSETS%
rem   assets_unpacked\ or assets\ next to this script
rem   bin\ (rscview.exe dropped into the data folder works the same way)
rem   the current directory
set "RSCVIEW=%~dp0bin\rscview.exe"
if not exist "%RSCVIEW%" (
    echo error: %RSCVIEW% not found - run build.bat first
    exit /b 1
)
if not defined RSCVIEW_ASSETS if exist "%~dp0assets_unpacked\" set "RSCVIEW_ASSETS=%~dp0assets_unpacked"
if not defined RSCVIEW_ASSETS if exist "%~dp0assets\ASSETS.DAT" set "RSCVIEW_ASSETS=%~dp0assets"
if not defined RSCVIEW_ASSETS if exist "%~dp0bin\ASSETS.DAT" set "RSCVIEW_ASSETS=%~dp0bin"
if not defined RSCVIEW_ASSETS if exist "%CD%\ASSETS.DAT" set "RSCVIEW_ASSETS=%CD%"
if not defined RSCVIEW_ASSETS (
    echo error: no game data found.  Set RSCVIEW_ASSETS to the folder holding ASSETS.DAT
    echo        ^(the disc files^) or to a loose asset tree, for example:
    echo          set RSCVIEW_ASSETS=D:\MC3
    exit /b 1
)
if "%RSCVIEW_ASSETS:~-1%"=="\" set "RSCVIEW_ASSETS=%RSCVIEW_ASSETS:~0,-1%"
if not exist "%RSCVIEW_ASSETS%\" (
    echo error: RSCVIEW_ASSETS=%RSCVIEW_ASSETS% is not a folder
    exit /b 1
)
set PATHARG=-path "%RSCVIEW_ASSETS%"
exit /b 0
