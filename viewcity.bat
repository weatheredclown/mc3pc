@echo off
setlocal
if not defined RSCVIEW_ASSETS if exist "%~dp0assets_unpacked" set "RSCVIEW_ASSETS=%~dp0assets_unpacked"
rem viewcity.bat [city] [rscview args]
rem   Interactive city model browser for Midnight Club 3:
rem     viewcity.bat sandiego
rem     viewcity.bat detroit -model 25
rem     viewcity.bat atlanta
rem     viewcity.bat tokyo
rem
rem   Controls:
rem     Left/Right Arrow : Previous / Next model
rem     Up/Down Arrow    : +/- 10 models
rem     PageUp/PageDown  : +/- 50 models
rem     Home/End         : First / Last model
rem     Space            : Toggle orbit rotation
rem     W                : Toggle wireframe mode
rem     L                : Toggle lighting / unlit
rem     Mouse Left Drag  : Orbit camera
rem     Mouse Wheel      : Zoom in / out

set HERE=%~dp0
set "CITY=sandiego"
if not "%~1"=="" (
    set "ARG1=%~1"
    if not "!ARG1:~0,1!"=="-" (
        set "CITY=%~1"
        shift
    )
)

"%HERE%bin\rscview.exe" -path "%RSCVIEW_ASSETS%" -city %CITY% %*