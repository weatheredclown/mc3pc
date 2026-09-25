# rscview build: compiles every file in sources.txt and links bin\rscview.exe.
# Run through build.bat (it sets up the MSVC x64 environment first).
#
#   build.bat            incremental (recompiles a directory when any of its sources is newer than its objs)
#   build.bat /clean     wipe this configuration's obj\ first
#   build.bat /release   /O2 (separate obj\release, bin\rscview_release.exe)
#   build.bat /includes  no build: list every header the sources include (obj\debug\includes.txt)
param([switch]$Clean, [switch]$Release, [switch]$Includes)

$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
$Cfg  = if ($Release) { 'release' } else { 'debug' }
$Obj  = Join-Path $Root "obj\$Cfg"
$Bin  = Join-Path $Root 'bin'
$Exe  = if ($Release) { 'rscview_release.exe' } else { 'rscview.exe' }

if ($Clean) {
	if (Test-Path $Obj) { Remove-Item -Recurse -Force $Obj }
}
New-Item -ItemType Directory -Force $Obj, $Bin | Out-Null

$Flags = @(
	'/nologo', '/c', '/MP', '/TP', '/W0', '/EHsc', '/permissive', '/Zc:forScope-', '/vmg', '/FS', '/MD',
	'/std:c++17', '/Z7',
	'/DWIN32', '/D_CONSOLE', '/D_MBCS', '/DNDEBUG', '/DOPNEW',
	'/D__WIN32=1', '/D__WIN32PC=1', '/D__D3D=1', '/D__PSX2=0', '/D__XBOX=0', '/D__GCUBE=0', '/D__XENON=0',
	'/D__OPENGL=0', '/D__DEV=1', '/D__BANK=0', '/D__OPTIMIZED=1', '/D__TOOL=0', '/D__FINAL=0',
	'/D__GAMESPY=0', '/D__XBOXLIVE=0', '/D__COMPILE_GAME=1', '/D__PAGING=1', '/D__USE_CRFANIMATION_LIB=1',
	"/I`"$Root\age\src`"", "/I`"$Root\mc3\src`"",
	"/FI`"$Root\age\src\core/opnew.h`""
)
if ($Release) { $Flags += '/O2' }

# Objects only know their source's timestamp, not the flags they were built with: when the
# flags change (a /D flipped), start this configuration over.
$Stamp = Join-Path $Obj 'flags.txt'
$FlagText = $Flags -join "`n"
if ((Test-Path $Stamp) -and ((Get-Content -Raw $Stamp).TrimEnd() -ne $FlagText)) {
	Write-Host "== compiler flags changed: rebuilding $Cfg from scratch"
	Remove-Item -Recurse -Force $Obj
	New-Item -ItemType Directory -Force $Obj | Out-Null
}
Set-Content -Encoding ascii $Stamp $FlagText

$Sources = Get-Content (Join-Path $Root 'sources.txt') | Where-Object { $_ -and -not $_.StartsWith('#') }
$Groups = $Sources | Group-Object { Split-Path -Parent $_ }

if ($Includes) {
	# every header the sources pull in, one "including file:" line each
	$rsp = Join-Path $Obj 'includes.rsp'
	(($Flags | Where-Object { $_ -ne '/c' }) + '/Zs' + '/showIncludes' + ($Sources | ForEach-Object { "`"$Root\$_`"" })) |
		Set-Content -Encoding ascii $rsp
	& cl "@$rsp" | Set-Content -Encoding ascii (Join-Path $Obj 'includes.txt')   # not '>': that writes UTF-16
	exit $LASTEXITCODE
}

$objs = New-Object System.Collections.Generic.List[string]
$failed = @()
foreach ($g in $Groups) {
	$outDir = Join-Path $Obj ($g.Name -replace '[\\/]', '_')
	New-Item -ItemType Directory -Force $outDir | Out-Null
	$stale = @()
	foreach ($s in $g.Group) {
		$src = Join-Path $Root $s
		$o = Join-Path $outDir ([IO.Path]::GetFileNameWithoutExtension($s) + '.obj')
		$objs.Add($o)
		if (-not (Test-Path $o) -or (Get-Item $src).LastWriteTime -gt (Get-Item $o).LastWriteTime) { $stale += $src }
	}
	if ($stale.Count -eq 0) { continue }
	Write-Host "== $($g.Name) ($($stale.Count))"
	$rsp = Join-Path $outDir 'cl.rsp'
	($Flags + ($stale | ForEach-Object { "`"$_`"" })) | Set-Content -Encoding ascii $rsp
	& cl "@$rsp" "/Fo$outDir\"
	if ($LASTEXITCODE -ne 0) { $failed += $g.Name }
}
if ($failed.Count) { Write-Host "FAILED: $($failed -join ' ')"; exit 1 }

# /FORCE:MULTIPLE: some game and engine objects define the same symbol.
$link = @('/NOLOGO', '/DEBUG', '/SUBSYSTEM:CONSOLE', '/FORCE:MULTIPLE', '/IGNORE:4006,4088',
	"/OUT:`"$Bin\$Exe`"", "/PDB:`"$Bin\$([IO.Path]::ChangeExtension($Exe, 'pdb'))`"") +
	($objs | ForEach-Object { "`"$_`"" }) +
	@('dsound.lib', 'dxguid.lib', 'winmm.lib', 'user32.lib', 'ole32.lib', 'gdi32.lib', 'comctl32.lib',
	  'comdlg32.lib', 'd3d11.lib', 'dxgi.lib', 'd3dcompiler.lib', 'ws2_32.lib')
$lrsp = Join-Path $Obj 'link.rsp'
$link | Set-Content -Encoding ascii $lrsp
Write-Host "== link $Exe"
& link "@$lrsp"
if ($LASTEXITCODE -ne 0) { exit 1 }
Write-Host "build ok: $Bin\$Exe"
