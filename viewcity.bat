@echo off
setlocal enabledelayedexpansion
call "%~dp0viewenv.bat" || exit /b 1
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

set "CITY=sandiego"
set "FIRST=%~1"
if defined FIRST if not "!FIRST:~0,1!"=="-" (
    set "CITY=%~1"
    shift
)
set "ARGS="
:collect
if "%~1"=="" goto run
set "ARGS=!ARGS! %1"
shift
goto collect

:run
"%RSCVIEW%" %PATHARG% -city %CITY% !ARGS!
