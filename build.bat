@echo off
rem Builds bin\rscview.exe with the newest Visual Studio that has the x64 C++ tools.
rem   build.bat [/clean] [/release] [/includes]
setlocal
set "HERE=%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo error: vswhere.exe not found - install Visual Studio with the C++ x64 tools & exit /b 1)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo error: no Visual Studio with the C++ x64 tools & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || exit /b 1
set ARGS=
:args
if "%~1"=="" goto run
if /i "%~1"=="/clean" set ARGS=%ARGS% -Clean
if /i "%~1"=="/release" set ARGS=%ARGS% -Release
if /i "%~1"=="/includes" set ARGS=%ARGS% -Includes
shift
goto args
:run
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%build.ps1" %ARGS%
exit /b %ERRORLEVEL%
