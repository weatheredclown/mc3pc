@echo off
setlocal enabledelayedexpansion
if not defined RSCVIEW_ASSETS if exist "%~dp0assets_unpacked" set "RSCVIEW_ASSETS=%~dp0assets_unpacked"
rem viewambient.bat [vehicle] [bodycolor] [rscview args]
rem   Renders one MC3 ambient (traffic) vehicle with rscview -ambient: the loose
rem   <assets>\vehicle\va_*\va_*.type loaded and drawn the way the game's
rem   traffic does it (drwShaderModel + skeleton bone palette + wheel bones).
rem
rem   vehicle    a va_* folder name, with or without the va_ prefix (default va_civic_sh)
rem   bodycolor  index into the vehicle's BodyColorTune list (tune\traffic\<vehicle>.vehicle)
rem
rem   viewambient.bat list                         lists every ambient vehicle
rem   viewambient.bat va_taxi_s
rem   viewambient.bat civic_sh 3 -wheelspin
rem   viewambient.bat va_cop_d -noorbit -yaw 90 -pitch 8 -shot cop.png -shotframes 3
rem
rem   Viewer switches: -list (print shaders, materials, bones, colours)  -nogfx
rem   -noshadow  -nowheels  -noground  -wheelspin [deg/s]  -yaw -pitch -dist -noorbit

cd /d "%~dp0"

set "VEH=va_civic_sh"
set "COLOR="
set "RES=-width 1280 -height 720"

for %%a in (%*) do (
    if /i "%%a"=="-width" set RES=
    if /i "%%a"=="-height" set RES=
)

if /i "%~1"=="list" (
    for /d %%d in (%RSCVIEW_ASSETS%\vehicle\va_*) do echo %%~nxd
    exit /b 0
)

set "FIRST=%~1"
if "%FIRST%"=="" goto run_default

if "!FIRST:~0,1!"=="-" (
    set "ARGS=%*"
    goto run
)
if "!FIRST:~0,1!"=="/" (
    set "ARGS=%*"
    goto run
)

set "VEH=%~1"
shift

if not "%~1"=="" (
    set "SECOND=%~1"
    if not "!SECOND:~0,1!"=="-" if not "!SECOND:~0,1!"=="/" (
        set "COLOR=%~1"
        shift
    )
)

set "ARGS="
:collect
if "%~1"=="" goto run
set "ARGS=!ARGS! %1"
shift
goto collect

:run_default
set "CMD=bin\rscview.exe -path %RSCVIEW_ASSETS% %RES% -ambient %VEH%"
goto exec

:run
set "CMD=bin\rscview.exe -path %RSCVIEW_ASSETS% %RES% -ambient %VEH%"
if defined COLOR set "CMD=!CMD! -bodycolor %COLOR%"
if defined ARGS set "CMD=!CMD! !ARGS!"

:exec
echo Running: !CMD!
!CMD!
