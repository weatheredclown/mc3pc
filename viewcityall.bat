@echo off
setlocal enabledelayedexpansion
rem viewcityall.bat [pack] [rscview args]
rem   Builds the whole city the way the game does and draws all of it at once.
rem     viewcityall.bat                          sd_midnight_clear
rem     viewcityall.bat atlanta_dusk_rainy
rem     viewcityall.bat sd_midnight_clear -citydist 1500
rem
rem   Mouse drag orbits, wheel zooms.  -citydist N overrides the draw distance.
call "%~dp0viewenv.bat" || exit /b 1
set "PACK=sd_midnight_clear"
set "FIRST=%~1"
if defined FIRST if not "!FIRST:~0,1!"=="-" (
    set "PACK=%~1"
    shift
)
set "ARGS="
:collect
if "%~1"=="" goto run
set "ARGS=!ARGS! %1"
shift
goto collect

:run
"%RSCVIEW%" %PATHARG% -pack resources/city/%PACK% -loadcity !ARGS!
