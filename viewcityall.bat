@echo off
setlocal
rem viewcityall.bat [pack] [rscview args]
rem   Builds the whole city the way the game does and draws all of it at once.
rem     viewcityall.bat                          sd_midnight_clear
rem     viewcityall.bat atlanta_dusk_rainy
rem     viewcityall.bat sd_midnight_clear -citydist 1500
rem
rem   Mouse drag orbits, wheel zooms.  -citydist N overrides the draw distance.
if not defined RSCVIEW_ASSETS if exist "%~dp0assets_unpacked" set "RSCVIEW_ASSETS=%~dp0assets_unpacked"
set HERE=%~dp0
set "PACK=sd_midnight_clear"
if not "%~1"=="" (
    set "ARG1=%~1"
    setlocal enabledelayedexpansion
    if not "!ARG1:~0,1!"=="-" (
        endlocal
        set "PACK=%~1"
        shift
    ) else endlocal
)
"%HERE%bin\rscview.exe" -path "%RSCVIEW_ASSETS%" -pack resources/city/%PACK% -loadcity %*
