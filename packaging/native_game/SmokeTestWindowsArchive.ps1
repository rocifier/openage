# Copyright 2026 the openage authors. See copying.md for legal info.
# Run from a VS developer PowerShell after building the strict native 7Z.
# This audits the *extracted archive*, not the CMake install tree.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $Archive,
    [string] $SevenZip = "7z.exe",
    [string] $Dumpbin = "dumpbin.exe",
    [switch] $KeepExtracted
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $Archive -PathType Leaf)) {
    throw "FVD native archive does not exist: $Archive"
}
if (-not (Get-Command $SevenZip -ErrorAction SilentlyContinue)) {
    throw "7-Zip command not found: $SevenZip"
}
$archivePath = (Resolve-Path -LiteralPath $Archive).Path
$temp = Join-Path ([IO.Path]::GetTempPath()) ("fvd-native-archive-" + [guid]::NewGuid().ToString("N"))
$extract = Join-Path $temp "extracted"
New-Item -ItemType Directory -Path $extract -Force | Out-Null

try {
    & $SevenZip x "-o$extract" -y $archivePath | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "7-Zip could not extract the native archive ($LASTEXITCODE)"
    }

    $launchers = @(Get-ChildItem -LiteralPath $extract -Recurse -File -Filter openage-native.exe |
        Where-Object { $_.Directory.Name -eq "bin" })
    if ($launchers.Count -ne 1) {
        throw "Expected exactly one bin/openage-native.exe; found $($launchers.Count)"
    }
    $game = $launchers[0].FullName
    $stage = $launchers[0].Directory.Parent.FullName
    $manifest = Join-Path $stage "share\openage\fvd-native-manifest.toml"
    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
        throw "Extracted release archive has no integrity manifest: $manifest"
    }

    & (Join-Path $PSScriptRoot "AuditWindowsImports.ps1") -Stage $stage -Dumpbin $Dumpbin
    if (-not $?) { throw "Windows DLL dependency audit failed" }

    function Invoke-GameCheck([string] $Name, [string[]] $Arguments,
                              [bool] $ShouldSucceed, [string] $ExpectedText,
                              [int] $TimeoutMs = 15000) {
        $stdout = Join-Path $temp "$Name.stdout"
        $stderr = Join-Path $temp "$Name.stderr"
        $argumentsQuoted = @($Arguments | ForEach-Object {
            if ($_.Contains(" ")) { '"' + $_ + '"' } else { $_ }
        })
        $process = Start-Process -FilePath $game -ArgumentList $argumentsQuoted -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        try {
            if (-not $process.WaitForExit($TimeoutMs)) {
                throw "$Name timed out (native launcher did not exit)"
            }
            $exitCode = $process.ExitCode
        }
        finally {
            if (-not $process.HasExited) {
                $process.Kill()
                $process.WaitForExit()
            }
            $process.Dispose()
        }
        $output = (Get-Content -LiteralPath $stdout -Raw) +
                  (Get-Content -LiteralPath $stderr -Raw)
        if (($exitCode -eq 0) -ne $ShouldSucceed) {
            throw "$Name unexpected exit $exitCode : $output"
        }
        if ($output -notmatch $ExpectedText) {
            throw "$Name missing expected output '$ExpectedText': $output"
        }
        Write-Host "PASS: $Name"
    }

    Invoke-GameCheck "help" @("--help") $true "Usage: openage-native"
    Invoke-GameCheck "version" @("--version") $true "openage-native"
    Invoke-GameCheck "bad-options" @("--network-update-interval-ms", "0") $false "invalid"
    Invoke-GameCheck "missing-assets" @("--verify-assets", "--asset-dir", (Join-Path $stage "nonexistent-assets")) $false "FVD modpack missing"
    Invoke-GameCheck "valid-assets" @("--verify-assets") $true "assets verified" 120000

    # A successful verification must not merely check that the manifest exists.
    # Deliberately corrupt a shipped asset, reject it, and then restore it.
    $modpack = Join-Path $stage "share\openage\fvd\modpack.toml"
    $original = [IO.File]::ReadAllBytes($modpack)
    try {
        [IO.File]::AppendAllText($modpack, [Environment]::NewLine + "# package tampering test")
        Invoke-GameCheck "tampered-assets" @("--verify-assets") $false "checksum mismatch" 120000
    }
    finally {
        [IO.File]::WriteAllBytes($modpack, $original)
    }
    Invoke-GameCheck "restored-assets" @("--verify-assets") $true "assets verified" 120000
    Write-Host "FVD native extracted archive CLI/integrity/DLL checks passed."
    Write-Host "Manual graphical solo, lobby, two-player and clean-exit checks remain required."
}
finally {
    if ($KeepExtracted) {
        Write-Host "Extracted archive retained for manual testing: $extract"
    }
    else {
        Remove-Item -LiteralPath $temp -Recurse -Force
    }
}
