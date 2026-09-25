@echo off
setlocal enabledelayedexpansion
call "%~dp0viewenv.bat" || exit /b 1

set "CAR=vp_lancer_04"
set "COLOR="
set "HAS_TARGET="
set "RES=-width 1280 -height 720"

for %%a in (%*) do (
    if /i "%%a"=="-car" set HAS_TARGET=1
    if /i "%%a"=="-pack" set HAS_TARGET=1
    if /i "%%a"=="-ppf" set HAS_TARGET=1
    if /i "%%a"=="/car" set HAS_TARGET=1
    if /i "%%a"=="/pack" set HAS_TARGET=1
    if /i "%%a"=="/ppf" set HAS_TARGET=1
    if /i "%%a"=="-width" set RES=
    if /i "%%a"=="-height" set RES=
)

if defined HAS_TARGET (
    "%RSCVIEW%" %PATHARG% %RES% %*
    exit /b %ERRORLEVEL%
)

set "FIRST=%~1"
if "%FIRST%"=="" goto run_default

if "!FIRST:~0,1!"=="-" (
    set "CAR=vp_lancer_04"
    set "ARGS=%*"
    goto run
)
if "!FIRST:~0,1!"=="/" (
    set "CAR=vp_lancer_04"
    set "ARGS=%*"
    goto run
)

set "CAR=%~1"
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
set "CMD="%RSCVIEW%" %PATHARG% %RES% -car %CAR%"
goto exec

:run
set "CMD="%RSCVIEW%" %PATHARG% %RES% -car %CAR%"
if defined COLOR set "CMD=!CMD! -carcolor %COLOR%"
if defined ARGS set "CMD=!CMD! !ARGS!"

:exec
echo Running: !CMD!
!CMD!
