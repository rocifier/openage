# Copyright 2026 the openage authors. See copying.md for legal info.
# Independent Windows packaging-audit regression test. No engine build needed.
$ErrorActionPreference = "Stop"
$root = Join-Path ([IO.Path]::GetTempPath()) ("fvd-audit-test-" + [guid]::NewGuid().ToString("N"))
$bin = Join-Path $root "stage\bin"
$audit = Join-Path $PSScriptRoot "AuditWindowsImports.ps1"
$mock = Join-Path $root "mock-dumpbin.ps1"

New-Item -Path $bin -ItemType Directory -Force | Out-Null
Set-Content -LiteralPath (Join-Path $bin "openage-native.exe") -Value "KERNEL32.dll"
Set-Content -LiteralPath (Join-Path $bin "openage-relay.exe") -Value "KERNEL32.dll"
Set-Content -LiteralPath $mock -Value @'
param([string] $Mode, [string] $Binary)
if ($Mode -ne "/DEPENDENTS") { throw "Unexpected mock dumpbin mode" }
Write-Output "Image has the following dependencies:"
Write-Output ("    " + (Get-Content -LiteralPath $Binary -TotalCount 1))
$global:LASTEXITCODE = 0
'@

function Expect-Pass {
    & $audit -Stage (Join-Path $root "stage") -Dumpbin $mock | Out-Null
}
function Expect-Failure([string] $Pattern) {
    $failed = $false
    try {
        & $audit -Stage (Join-Path $root "stage") -Dumpbin $mock | Out-Null
    }
    catch {
        if ($_.Exception.Message -notmatch $Pattern) {
            throw "Unexpected audit failure (expected $Pattern): $_"
        }
        $failed = $true
    }
    if (-not $failed) { throw "Native package audit accepted invalid fixture: $Pattern" }
}

try {
    Expect-Pass
    Set-Content -LiteralPath (Join-Path $bin "openage-native.exe") -Value "fvd_missing_dependency.dll"
    Expect-Failure "Missing non-system DLL dependency"
    Set-Content -LiteralPath (Join-Path $bin "openage-native.exe") -Value "python312.dll"
    Expect-Failure "imports Python"
    Set-Content -LiteralPath (Join-Path $bin "openage-native.exe") -Value "vcruntime140.dll"
    Expect-Failure "Missing non-system DLL dependency"
    Set-Content -LiteralPath (Join-Path $bin "vcruntime140.dll") -Value "KERNEL32.dll"
    Expect-Pass
    New-Item -ItemType File -Path (Join-Path $bin "python312.zip") | Out-Null
    Expect-Failure "Python runtime/developer files"
    Remove-Item -LiteralPath (Join-Path $bin "python312.zip")
    New-Item -Path (Join-Path $root "stage\bin\site-packages") -ItemType Directory | Out-Null
    Expect-Failure "Python runtime directories"
    Remove-Item -LiteralPath (Join-Path $root "stage\bin\site-packages") -Recurse
    Expect-Pass
    Write-Host "Native Windows dependency-audit fixtures passed."
}
finally {
    Remove-Item -LiteralPath $root -Recurse -Force
}
