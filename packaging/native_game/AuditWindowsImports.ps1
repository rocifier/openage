# Copyright 2026 the openage authors. See copying.md for legal info.
# Run from a Visual Studio developer PowerShell (or after deps/msvc-env.ps1).
# Check all extracted FVDNative EXE/DLL files, including Qt plugins.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $Stage,
    [string] $Dumpbin = "dumpbin.exe"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Stage -PathType Container)) {
    throw "Native FVD stage does not exist: $Stage"
}
if (-not (Get-Command $Dumpbin -ErrorAction SilentlyContinue)) {
    throw "dumpbin.exe is unavailable. Source .\deps\msvc-env.ps1 first."
}

$stageRoot = (Resolve-Path -LiteralPath $Stage).Path
$gameExe = Join-Path $stageRoot "bin\openage-native.exe"
$relayExe = Join-Path $stageRoot "bin\openage-relay.exe"
foreach ($required in @($gameExe, $relayExe)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Native FVD executable is missing: $required"
    }
}

$pythonFiles = @(Get-ChildItem -LiteralPath $stageRoot -File -Recurse -Force |
    Where-Object {
        $_.Extension -match '^\.(py|pyc|pyo|pyd|whl|pth)$' -or
        $_.Name -match '^(lib)?python\d+(?:_d)?\.(dll|exe|lib|zip|_pth)$' -or
        $_.Name -match '^pythonw?\.exe$'
    })
if ($pythonFiles.Count -gt 0) {
    throw "Python runtime/developer files found: $($pythonFiles.FullName -join ', ')"
}
$pythonDirectories = @(Get-ChildItem -LiteralPath $stageRoot -Directory -Recurse -Force |
    Where-Object { $_.Name -match '^(?i:__pycache__|site-packages|dist-packages|python[0-9][0-9._-]*)$' })
if ($pythonDirectories.Count -gt 0) {
    throw "Python runtime directories found: $($pythonDirectories.FullName -join ', ')"
}

# Do not accept non-system DLLs supplied by PATH on the build machine.
# Qt plugin dependencies may live beside the plugin or beside openage-native.
$binRoot = Join-Path $stageRoot "bin"
$systemRoot = Join-Path $env:WINDIR "System32"
if (-not (Test-Path -LiteralPath $systemRoot -PathType Container)) {
    throw "Cannot locate Windows System32 for dependency auditing"
}

function Test-PackagedDependency([string] $Name, [string] $BinaryDirectory) {
    foreach ($directory in @($BinaryDirectory, $binRoot)) {
        if (Test-Path -LiteralPath (Join-Path $directory $Name) -PathType Leaf) {
            return $true
        }
    }
    if ($Name -match '^(?i:api-ms-win-|ext-ms-win-)') {
        # API-set names are resolved by the Windows loader, not by DLL files.
        return $true
    }
    if ($Name -match '^(?i:vcruntime|msvcp|concrt|vccorlib|msvcr)[0-9_]*[.]dll$') {
        # MSVC redistributables must ship with the package, not the builder.
        return $false
    }
    return (Test-Path -LiteralPath (Join-Path $systemRoot $Name) -PathType Leaf)
}

$binaries = @(Get-ChildItem -LiteralPath $stageRoot -File -Recurse -Force |
    Where-Object { $_.Extension -match '^\.(exe|dll)$' })
foreach ($binary in $binaries) {
    $output = & $Dumpbin /DEPENDENTS $binary.FullName 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed for $($binary.FullName): $output"
    }
    $imports = [regex]::Matches($output, '(?im)^\s*([a-z0-9_.+-]+\.dll)\s*$')
    foreach ($import in $imports) {
        $dependency = $import.Groups[1].Value
        if ($dependency -match '^(?i:(?:lib)?python\d*(?:_d)?)[.]dll$') {
            throw "$($binary.FullName) imports Python: $dependency"
        }
        if (-not (Test-PackagedDependency $dependency $binary.DirectoryName)) {
            throw "Missing non-system DLL dependency: $($binary.FullName) -> $dependency"
        }
    }
}
Write-Host "FVDNative audit passed: $($binaries.Count) Windows binaries; no Python; all non-system DLL imports bundled."
