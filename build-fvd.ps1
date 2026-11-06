# Copyright 2026 the openage authors. See copying.md for legal info.
# One-command Windows FVD native release build for an MSVC/vcpkg checkout.
# Normal use: .\build-fvd.cmd
# Re-export a changed engine API: .\build-fvd.cmd -ReexportApi
# Modes: package (default), test, solo, relay. Accept native options directly
# after solo/relay, e.g. build-fvd.cmd solo --no-audio.
# Manual parsing preserves unknown --native-options under Windows PowerShell 5.1.
$Mode = "package"
$ReexportApi = $false
$SkipSmoke = $false
$ObserveSoloSeconds = 30
$Jobs = 4
$NativeArgs = @()
$tokens = @($args)
$i = 0
if ($tokens.Count -gt 0 -and $tokens[0] -in @("package", "test", "solo", "relay")) {
    $Mode = $tokens[0]
    $i = 1
}
while ($i -lt $tokens.Count) {
    $token = [string] $tokens[$i]
    switch ($token) {
        "-ReexportApi" { $ReexportApi = $true; break }
        "-SkipSmoke" { $SkipSmoke = $true; break }
        "-ObserveSoloSeconds" {
            if ($i + 1 -ge $tokens.Count) { throw "$token requires a number" }
            $i++
            $ObserveSoloSeconds = [int] $tokens[$i]
            if ($ObserveSoloSeconds -lt 0 -or $ObserveSoloSeconds -gt 600) {
                throw "-ObserveSoloSeconds must be 0..600"
            }
            break
        }
        "-Jobs" {
            if ($i + 1 -ge $tokens.Count) { throw "$token requires a number" }
            $i++
            $Jobs = [int] $tokens[$i]
            if ($Jobs -lt 1 -or $Jobs -gt 64) { throw "-Jobs must be 1..64" }
            break
        }
        "--" {
            if ($i + 1 -lt $tokens.Count) {
                $NativeArgs += @($tokens[($i + 1)..($tokens.Count - 1)])
            }
            $i = $tokens.Count
            continue
        }
        default {
            if ($Mode -in @("solo", "relay")) {
                $NativeArgs += $token
            }
            else {
                throw "Unknown argument '$token'. Commands: package, test, solo, relay."
            }
        }
    }
    $i++
}

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSCommandPath
$build = Join-Path $root ".bin\msvc-vcpkg"
$converted = Join-Path $root "assets\converted"
$api = Join-Path $converted "engine"
$dist = Join-Path $root "dist"

function Invoke-Checked([string] $Executable, [string[]] $Arguments, [string] $Description) {
    Write-Host ""
    Write-Host "==> $Description" -ForegroundColor Cyan
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed (exit code $LASTEXITCODE)."
    }
}

function Find-SevenZip {
    $command = Get-Command "7z.exe" -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($base in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if (-not $base) { continue }
        $candidate = Join-Path $base "7-Zip\7z.exe"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    throw "7-Zip not found. Install 7-Zip (7z.exe), or rerun with -SkipSmoke."
}

function Find-Dumpbin {
    $command = Get-Command "dumpbin.exe" -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    if ($env:VCToolsInstallDir) {
        $candidate = Join-Path $env:VCToolsInstallDir "bin\Hostx64\x64\dumpbin.exe"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }

    # A configured MSVC checkout already knows the VS linker location.
    $cache = Join-Path $build "CMakeCache.txt"
    if (Test-Path -LiteralPath $cache -PathType Leaf) {
        $line = Get-Content -LiteralPath $cache |
            Where-Object { $_ -match '^CMAKE_LINKER:[^=]*=' } |
            Select-Object -First 1
        if ($line) {
            $linker = ($line -split '=', 2)[1]
            $candidate = Join-Path (Split-Path -Parent $linker) "dumpbin.exe"
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
        }
    }

    foreach ($base in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if (-not $base) { continue }
        $vs = Join-Path $base "Microsoft Visual Studio"
        if (-not (Test-Path -LiteralPath $vs -PathType Container)) { continue }
        $found = Get-ChildItem -LiteralPath $vs -Filter dumpbin.exe -File -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match 'Hostx64\\x64\\dumpbin[.]exe$' } |
            Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    throw "Visual Studio dumpbin.exe not found. Install the MSVC C++ build tools."
}

function Get-LatestArchive {
    if (-not (Test-Path -LiteralPath $dist -PathType Container)) {
        throw "No package found. First run: .\build-fvd.cmd"
    }
    $archive = Get-ChildItem -LiteralPath $dist -Filter "*.7z" -File |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if (-not $archive) { throw "No native 7Z found in $dist. First run: .\build-fvd.cmd" }
    return $archive
}

function Get-ExtractedRunStage {
    $archive = Get-LatestArchive
    $sevenZip = Find-SevenZip
    $runDir = Join-Path $dist "run"
    $stampFile = Join-Path $runDir ".archive-stamp"
    $expectedStamp = "$($archive.FullName):$($archive.Length):$($archive.LastWriteTimeUtc.Ticks)"
    $actualStamp = ""
    if (Test-Path -LiteralPath $stampFile -PathType Leaf) {
        $actualStamp = Get-Content -LiteralPath $stampFile -Raw
    }
    if ($actualStamp.Trim() -ne $expectedStamp) {
        Write-Host "==> Extracting latest native archive to $runDir" -ForegroundColor Cyan
        if (Test-Path -LiteralPath $runDir) {
            Remove-Item -LiteralPath $runDir -Recurse -Force
        }
        New-Item -ItemType Directory -Path $runDir -Force | Out-Null
        & $sevenZip x "-o$runDir" -y $archive.FullName | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "7-Zip failed to extract the archive" }
        Set-Content -LiteralPath $stampFile -Value $expectedStamp
    }
    $games = @(Get-ChildItem -LiteralPath $runDir -Recurse -File -Filter "openage-native.exe" |
        Where-Object { $_.Directory.Name -eq "bin" })
    if ($games.Count -ne 1) { throw "Expected one packaged openage-native.exe; found $($games.Count)" }
    return $games[0].Directory.FullName
}

Push-Location $root
try {
    if ($Mode -in @("solo", "relay")) {
        # Run the actual packaged binary, not a developer launcher. No compilation.
        $bin = Get-ExtractedRunStage
        $name = if ($Mode -eq "solo") { "openage-native.exe" } else { "openage-relay.exe" }
        $exe = Join-Path $bin $name
        $runArgs = @()
        if ($Mode -eq "solo") { $runArgs += "--solo" }
        $runArgs += $NativeArgs
        Write-Host "==> Running packaged $Mode. Use Ctrl+C to stop." -ForegroundColor Cyan
        Push-Location $bin
        try {
            & $exe @runArgs
            if ($LASTEXITCODE -ne 0) { throw "$Mode exited with code $LASTEXITCODE" }
        }
        finally { Pop-Location }
        return
    }

    # This optional local developer environment is not shipped in the release.
    $envScript = Join-Path $root "deps\msvc-env.ps1"
    if (Test-Path -LiteralPath $envScript -PathType Leaf) {
        . $envScript
    }
    if (-not (Get-Command "cmake.exe" -ErrorAction SilentlyContinue)) {
        throw "CMake not found. Set up the Windows MSVC/vcpkg development environment first."
    }
    if (-not (Get-Command "cpack.exe" -ErrorAction SilentlyContinue)) {
        throw "CPack not found; it is installed with CMake."
    }

    if ($Mode -eq "test") {
        if (-not (Test-Path -LiteralPath (Join-Path $build "CMakeCache.txt") -PathType Leaf)) {
            throw "No configured Windows build; first run: .\build-fvd.cmd"
        }
        Invoke-Checked "cmake.exe" @("--build", $build, "--config", "RelWithDebInfo",
            "--target", "openage_native_game_args_test", "openage_native_relay_args_test",
            "openage_native_relay_network_test", "--parallel", "$Jobs") "Build native test targets"
        Invoke-Checked "ctest.exe" @("--test-dir", $build, "-C", "RelWithDebInfo",
            "-R", "^fvd_native_(game|relay)_", "--output-on-failure") "Run native CTest suite"
        $archiveAvailable = (Test-Path -LiteralPath $dist -PathType Container) -and
            (@(Get-ChildItem -LiteralPath $dist -Filter "*.7z" -File).Count -gt 0)
        if (-not $SkipSmoke -and $archiveAvailable) {
            $archive = Get-LatestArchive
            & (Join-Path $root "packaging\native_game\SmokeTestWindowsArchive.ps1") `
                -Archive $archive.FullName -SevenZip (Find-SevenZip) -Dumpbin (Find-Dumpbin) `
                -ObserveSoloSeconds $ObserveSoloSeconds
            if (-not $?) { throw "Extracted archive smoke test failed" }
        }
        Write-Host "FVD native tests passed." -ForegroundColor Green
        return
    }

    # Developer-only asset export is needed once per API version, not on
    # every player build. The shipped game never needs a Python runtime.
    $apiMissing = -not ((Test-Path -LiteralPath (Join-Path $api "modpack.toml") -PathType Leaf) -and
                         (Test-Path -LiteralPath (Join-Path $api "manifest.toml") -PathType Leaf))
    if ($apiMissing -or $ReexportApi) {
        Write-Host "Exported engine API is missing or refresh was requested." -ForegroundColor Yellow
        Invoke-Checked "cmake.exe" @("-S", $root, "-B", $build,
            "-DFVD_NATIVE_RELEASE=OFF", "-DFVD_WITH_PYTHON_ADAPTERS=ON") "Configure developer API exporter"
        Invoke-Checked "cmake.exe" @("--build", $build, "--config", "RelWithDebInfo",
            "--parallel", "$Jobs") "Build developer tools for API export"

        $runner = Join-Path $build "RelWithDebInfo\run.exe"
        if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) {
            throw "Developer API exporter missing: $runner"
        }
        Push-Location $build
        try {
            Invoke-Checked $runner @("convert-export-api", $converted) "Export engine nyan API"
        }
        finally {
            Pop-Location
        }
    } else {
        Write-Host "==> Using existing exported engine API (no Python developer rebuild)." -ForegroundColor Green
    }

    if (-not ((Test-Path -LiteralPath (Join-Path $api "modpack.toml") -PathType Leaf) -and
               (Test-Path -LiteralPath (Join-Path $api "manifest.toml") -PathType Leaf))) {
        throw "Engine API export is incomplete in $api"
    }

    Invoke-Checked "cmake.exe" @("-S", $root, "-B", $build,
        "-DFVD_NATIVE_RELEASE=ON", "-DFVD_WITH_PYTHON_ADAPTERS=OFF",
        "-DFVD_NATIVE_API_ASSET_DIR=$api") "Configure Python-free native release"
    Invoke-Checked "cmake.exe" @("--build", $build, "--config", "RelWithDebInfo",
        "--target", "openage_native_game", "openage_native_relay",
        "--parallel", "$Jobs") "Build native game and relay"

    New-Item -ItemType Directory -Path $dist -Force | Out-Null
    Invoke-Checked "cpack.exe" @("--config", (Join-Path $build "CPackConfig.cmake"),
        "-C", "RelWithDebInfo", "-G", "7Z", "-B", $dist) "Package Python-free Windows 7Z"

    $archive = Get-ChildItem -LiteralPath $dist -Filter "*.7z" -File |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if (-not $archive) {
        throw "CPack finished but no 7Z archive was found in $dist"
    }
    Write-Host ""
    Write-Host "Package: $($archive.FullName)" -ForegroundColor Green

    if (-not $SkipSmoke) {
        $sevenZip = Find-SevenZip
        $dumpbin = Find-Dumpbin
        Write-Host ""
        Write-Host "==> Audit extracted archive, CLI, assets, and solo process" -ForegroundColor Cyan
        & (Join-Path $root "packaging\native_game\SmokeTestWindowsArchive.ps1") `
            -Archive $archive.FullName -SevenZip $sevenZip -Dumpbin $dumpbin `
            -KeepExtracted -ObserveSoloSeconds $ObserveSoloSeconds
        if (-not $?) { throw "Extracted archive smoke test failed." }
    }
    else {
        Write-Host "Smoke test skipped (-SkipSmoke)." -ForegroundColor Yellow
    }

    Write-Host ""
    Write-Host "FVD native release package is ready: $($archive.FullName)" -ForegroundColor Green
    Write-Host "Before publishing, complete the manual gameplay/multiplayer and clean-machine checks."
}
finally {
    Pop-Location
}
