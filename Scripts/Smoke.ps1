# End-to-end smoke test: unit tests, runtime/editor launch with screenshots, export and run of the exported game.
# Fails (non-zero exit) on any crash, non-zero exit code, missing or blank screenshot.
# Usage: Scripts/Smoke.ps1 [-Config Debug|Release] [-SkipBuild]
param(
	[string]$Config = "Debug",
	[switch]$SkipBuild
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if(-not $SkipBuild) {
	& "$PSScriptRoot/Build.ps1" -Config $Config
	if($LASTEXITCODE -ne 0) { throw "build failed" }
}
$bin = Join-Path $root "Build/$Config/bin"
$out = Join-Path $root "Out/Smoke"
Remove-Item -Recurse -Force $out -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $out | Out-Null

function Run([string]$exe, [string[]]$arguments, [string]$what) {
	$params = @{ FilePath = $exe; Wait = $true; PassThru = $true; NoNewWindow = $true; RedirectStandardOutput = "$out/$what.log"; RedirectStandardError = "$out/$what.err.log" }
	if($arguments.Count -gt 0) { $params.ArgumentList = $arguments }
	$p = Start-Process @params
	if($p.ExitCode -ne 0) { throw "$what exited with code $($p.ExitCode) (see $out/$what.err.log)" }
}

function AssertImage([string]$path) {
	if(-not (Test-Path $path)) { throw "missing screenshot $path" }
	if((Get-Item $path).Length -lt 20000) { throw "screenshot $path looks blank (too small)" }
}

Write-Host "== unit tests"
Run "$bin/StarfallTests.exe" @() "tests"

$project = Join-Path $root "Projects/TestProject/TestProject.sfproj"

Write-Host "== runtime"
Run "$bin/StarfallRuntime.exe" @("--project", $project, "--frames", "60", "--screenshot", "$out/runtime.png", "--width", "1280", "--height", "720") "runtime"
AssertImage "$out/runtime.png"

Write-Host "== editor (edit mode, then play mode)"
Run "$bin/StarfallEditor.exe" @("--project", $project, "--select", "GoldSphere", "--frames", "60", "--screenshot", "$out/editor.png", "--width", "1700", "--height", "950") "editor"
AssertImage "$out/editor.png"
Run "$bin/StarfallEditor.exe" @("--project", $project, "--play", "--frames", "90", "--screenshot", "$out/editor_play.png", "--width", "1700", "--height", "950") "editor_play"
AssertImage "$out/editor_play.png"

Write-Host "== export and run exported game"
Run "$bin/StarfallEditor.exe" @("--export", $project, "$out/Export") "export"
$exported = Get-ChildItem "$out/Export" -Filter "TestProject*" | Where-Object { $_.Extension -in ".exe", "" } | Select-Object -First 1
if(-not $exported) { throw "exported executable not found" }
Run $exported.FullName @("--frames", "60", "--screenshot", "$out/exported.png", "--width", "960", "--height", "540") "exported"
AssertImage "$out/exported.png"

Write-Host "Smoke test passed. Screenshots in $out"
