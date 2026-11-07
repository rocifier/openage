# Copyright 2026 the openage authors. See copying.md for legal info.
# One-command Windows FVD native release build for an MSVC/vcpkg checkout.
# Normal use: .\build-fvd.cmd
# Engine API is checked in under assets/fvd/engine_api; no export step.
# Modes: package (default), build, test, python-test, solo, relay. Accept runtime or
# legacy test options directly after their mode.
# Manual parsing preserves unknown --native-options under Windows PowerShell 5.1.
$Mode = "package"
$SkipSmoke = $false
$ObserveSoloSeconds = 30
$Jobs = 4
$NativeArgs = @()
$tokens = @($args)
$i = 0
if ($tokens.Count -gt 0 -and
    $tokens[0] -in @("package", "build", "test", "python-test", "solo", "relay")) {
    $Mode = $tokens[0]
    $i = 1
}
while ($i -lt $tokens.Count) {
    $token = [string] $tokens[$i]
    switch ($token) {
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
            if ($Mode -in @("python-test", "solo", "relay", "test")) {
                $NativeArgs += $token
            }
            else {
                throw "Unknown argument '$token'. Commands: package, build, test, python-test, solo, relay."
            }
        }
    }
    $i++
}

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSCommandPath
$build = Join-Path $root ".bin\msvc-vcpkg"
$api = Join-Path $root "assets\fvd\engine_api"
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

function Get-CompatibleWindowsArchives {
    if (-not (Test-Path -LiteralPath $dist -PathType Container)) { return @() }

    $sevenZip = Find-SevenZip
    $compatible = @()
    foreach ($archive in @(Get-ChildItem -LiteralPath $dist -Filter "*.7z" -File)) {
        $listing = & $sevenZip l -ba -slt $archive.FullName 2>$null
        if ($LASTEXITCODE -eq 0 -and
            $listing -match '^Path = (?:.*[\\/])?bin[\\/]openage-native[.]exe$') {
            $compatible += $archive
        }
    }
    return $compatible
}

function Get-LatestArchive {
    $archive = @(Get-CompatibleWindowsArchives) |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if (-not $archive) {
        throw "No Windows native 7Z found in $dist. First run: .\build-fvd.cmd"
    }
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

    # Pure Python tests are optional tools, wholly independent of CMake,
    # the native engine and the old Cython test dispatcher.
    if ($Mode -eq "python-test") {
        $python = Join-Path $root ".venv\Scripts\python.exe"
        if (-not (Test-Path -LiteralPath $python -PathType Leaf)) {
            $command = Get-Command "python.exe" -ErrorAction SilentlyContinue
            if (-not $command) { throw "Python unavailable; install Python 3.12+ or run 'uv sync'." }
            $python = $command.Source
        }
        $testArgs = @($NativeArgs)
        if ($testArgs.Count -eq 0) {
            $testArgs = @("openage.testing.test_fvd_network_benchmark")
        }
        Invoke-Checked $python (@("-m", "unittest", "-v") + $testArgs) "Run pure Python tests"
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
    if ($Mode -eq "test") {
        if (-not (Test-Path -LiteralPath (Join-Path $build "CMakeCache.txt") -PathType Leaf)) {
            throw "No configured Windows build; first run: .\build-fvd.cmd build"
        }
        Invoke-Checked "cmake.exe" @("-S", $root, "-B", $build,
            "-DFVD_NATIVE_RELEASE=OFF") "Configure native-only C++ tests"
        Invoke-Checked "cmake.exe" @("--build", $build, "--config", "RelWithDebInfo",
            "--target", "openage_native_game_args_test", "openage_native_relay_args_test",
            "openage_native_relay_network_test", "openage_native_cpp_tests", "--parallel", "$Jobs") "Build native test targets"
        Invoke-Checked "ctest.exe" @("--test-dir", $build, "-C", "RelWithDebInfo",
            "-R", "^fvd_native_(game|relay)_", "--output-on-failure") "Run native CTest suite"
        if ($NativeArgs.Count -gt 0) {
            $nativeRunner = Join-Path $build "packaging\RelWithDebInfo\openage_native_cpp_tests.exe"
            if (-not (Test-Path -LiteralPath $nativeRunner -PathType Leaf)) {
                $nativeRunner = Join-Path $build "packaging\openage_native_cpp_tests.exe"
            }
            Invoke-Checked $nativeRunner @($NativeArgs) "Run selected native C++ tests"
        }
        Write-Host "FVD native tests passed." -ForegroundColor Green
        return
    }

    # The engine API is a committed source asset. No Python/Cython export
    # bootstrap may run in a supported native build or package command.
    foreach ($required in @("modpack.toml", "manifest.toml", "root.nyan")) {
        if (-not (Test-Path -LiteralPath (Join-Path $api $required) -PathType Leaf)) {
            throw "Tracked FVD engine API incomplete: missing $required under $api"
        }
    }

    $nativeRelease = if ($Mode -eq "package") { "ON" } else { "OFF" }
    $configureDescription = if ($Mode -eq "package") {
        "Configure Python-free native release"
    }
    else {
        "Configure Python-free native developer build"
    }
    Invoke-Checked "cmake.exe" @("-S", $root, "-B", $build,
        "-DFVD_NATIVE_RELEASE=$nativeRelease") $configureDescription
    Invoke-Checked "cmake.exe" @("--build", $build, "--config", "RelWithDebInfo",
        "--target", "openage_native_game", "openage_native_relay",
        "--parallel", "$Jobs") "Build native game and relay"

    if ($Mode -eq "build") {
        Write-Host "FVD native game and relay build completed." -ForegroundColor Green
        return
    }

    if (-not (Get-Command "cpack.exe" -ErrorAction SilentlyContinue)) {
        throw "CPack not found; it is installed with CMake."
    }

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
