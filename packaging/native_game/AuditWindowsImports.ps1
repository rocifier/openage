# Copyright 2026 the openage authors. See copying.md for legal info.
# Run from a Visual Studio developer PowerShell (or after deps/msvc-env.ps1).
# Scans every shipped Windows executable/DLL, including Qt plugins.

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

$pythonFiles = @(Get-ChildItem -LiteralPath $stageRoot -File -Recurse |
    Where-Object {
        $_.Extension -match '^(?i)\.(py|pyc|pyo|pyd|whl)$' -or
        $_.Name -match '^(?i)(lib)?python\d*(?:_d)?\.(dll|exe|lib)$'
    })
if ($pythonFiles.Count -gt 0) {
    throw "Python runtime/developer files found: $($pythonFiles.FullName -join ', ')"
}

$binaries = @(Get-ChildItem -LiteralPath $stageRoot -File -Recurse |
    Where-Object { $_.Extension -match '^(?i)\.(exe|dll)$' })
foreach ($binary in $binaries) {
    $output = & $Dumpbin /DEPENDENTS $binary.FullName 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed for $($binary.FullName): $output"
    }
    $imports = [regex]::Matches($output, '(?im)^\s*([a-z0-9_.-]+\.dll)\s*$')
    foreach ($import in $imports) {
        if ($import.Groups[1].Value -match '^(?i)(?:lib)?python\d*(?:_d)?\.dll$') {
            throw "$($binary.FullName) imports Python: $($import.Groups[1].Value)"
        }
    }
}
Write-Host "FVDNative dependency audit passed: $($binaries.Count) Windows binaries, no Python imports/files."
