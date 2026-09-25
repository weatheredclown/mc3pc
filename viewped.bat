@echo off
setlocal enabledelayedexpansion
call "%~dp0viewenv.bat" || exit /b 1
rem viewped.bat [ped] [animation] [rscview args]
rem   Renders one MC3 city pedestrian playing an animation: the resource viewer's
rem   orbit camera around testanim's playback loop.  The ped's model and skeleton
rem   come out of <assets>\resources\city\<city>_peds.pck through the
rem   game's own loader (mcCreatureTypeManager); the clip is a loose .anim under
rem   ped\anim, and tune\ped\<city>\<ped>.cal says which clip is which.
rem
rem   ped        a creature type of the city - a name, part of one, or an index
rem              (default: the city's first, SANmped01)
rem   animation  an animation type from the ped's .cal ("walk", "run", "idle",
rem              "yell", ...) or the name of a file under ped\anim (default walk)
rem
rem   viewped.bat                                  SANmped01 walking
rem   viewped.bat list                             every ped and clip of the city
rem   viewped.bat fped01 run
rem   viewped.bat SANmped02 idle -city sd
rem   viewped.bat 0 walk -stride                   let the walk cycle travel
rem   viewped.bat 0 walk -noorbit -yaw 35 -pitch 6 -shot ped.png -shotframes 35
rem
rem   Viewer switches: -city <sd|atlanta|detroit|tokyo>  -list (peds, clips, bones,
rem   lods)  -nogfx  -noground  -pause  -frame <n>  -animrate <x>  -stride
rem   -lod <0-3>  -bucket <n>  -variant <n>  -yaw -pitch -dist -noorbit -bg

cd /d "%~dp0"

set "PED="
set "ANIM="
set "RES=-width 1280 -height 720"

for %%a in (%*) do (
    if /i "%%a"=="-width" set RES=
    if /i "%%a"=="-height" set RES=
)

rem "viewped list" prints the city's peds and their clips without opening a window.
if /i "%~1"=="list" (
    "%RSCVIEW%" %PATHARG% -ped -nogfx -list %2 %3 %4 %5
    exit /b %ERRORLEVEL%
)

set "FIRST=%~1"
if "%FIRST%"=="" goto run
if "!FIRST:~0,1!"=="-" goto collect_only
if "!FIRST:~0,1!"=="/" goto collect_only

set "PED=%~1"
shift

if not "%~1"=="" (
    set "SECOND=%~1"
    if not "!SECOND:~0,1!"=="-" if not "!SECOND:~0,1!"=="/" (
        set "ANIM=%~1"
        shift
    )
)

:collect_only
set "ARGS="
:collect
if "%~1"=="" goto run
set "ARGS=!ARGS! %1"
shift
goto collect

:run
set "CMD="%RSCVIEW%" %PATHARG% %RES% -ped"
if defined PED  set "CMD=!CMD! !PED!"
if not defined PED set "CMD=!CMD! 0"
if defined ANIM set "CMD=!CMD! -anim !ANIM!"
if defined ARGS set "CMD=!CMD! !ARGS!"

echo Running: !CMD!
!CMD!
