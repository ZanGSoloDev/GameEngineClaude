# Configures and builds Starfall on Windows using the Visual Studio toolchain + Ninja.
# Usage: Scripts/Build.ps1 [-Config Debug|Release] [-Target <name>] [-Test] [-Reconfigure]
param(
	[string]$Config = "Debug",
	[string]$Target = "",
	[switch]$Test,
	[switch]$Reconfigure
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "Build/$Config"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -property installationPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$cmakeDir = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake"
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if(-not $cmake) { $cmake = Join-Path $cmakeDir "CMake\bin\cmake.exe" }
$ninjaDir = Join-Path $cmakeDir "Ninja"

$script = @"
call "$vcvars" >nul 2>nul
set PATH=$ninjaDir;%PATH%
"$cmake" -S "$root" -B "$buildDir" -G Ninja -DCMAKE_BUILD_TYPE=$Config
if errorlevel 1 exit /b 1
"$cmake" --build "$buildDir" $(if($Target){"--target $Target"})
if errorlevel 1 exit /b 1
$(if($Test){"ctest --test-dir `"$buildDir`" --output-on-failure"})
"@
if($Reconfigure -and (Test-Path $buildDir)) { Remove-Item -Recurse -Force (Join-Path $buildDir "CMakeCache.txt") -ErrorAction SilentlyContinue }
$bat = Join-Path $env:TEMP "starfall_build.bat"
Set-Content -Path $bat -Value $script -Encoding ASCII
$ErrorActionPreference = "Continue"
cmd /c $bat
exit $LASTEXITCODE
