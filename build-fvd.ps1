# Copyright 2026 the openage authors. See copying.md for legal info.
# Windows FVD developer build/run; package is an explicit release-only operation.
# Normal use: .\build-fvd.cmd
# Engine API is checked in under assets/fvd/engine_api; no export step.
# Modes: build (default), game, solo, relay, test, python-test, package.
# test and native runtime options directly after their mode.
# Manual parsing preserves unknown --native-options under Windows PowerShell 5.1.
$Mode = "build"
$Jobs = 4
$NativeArgs = @()
$tokens = @($args)
$i = 0
if ($tokens.Count -gt 0 -and
    $tokens[0] -in @("package", "build", "test", "python-test", "game", "solo", "relay")) {
    $Mode = $tokens[0]
    $i = 1
}
while ($i -lt $tokens.Count) {
    $token = [string] $tokens[$i]
    switch ($token) {
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
            if ($Mode -in @("python-test", "game", "solo", "relay", "test")) {
                $NativeArgs += $token
            }
            else {
                throw "Unknown argument '$token'. Commands: package, build, test, python-test, game, solo, relay."
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

function Get-CMakeCacheValue([string] $Key) {
    $cache = Join-Path $build "CMakeCache.txt"
    if (-not (Test-Path -LiteralPath $cache -PathType Leaf)) { return $null }
    $pattern = "^$([regex]::Escape($Key)):[^=]*=(.*)$"
    $line = Get-Content -LiteralPath $cache |
        Where-Object { $_ -match $pattern } | Select-Object -First 1
    if ($line -and $line -match $pattern) { return $Matches[1] }
    return $null
}

function Ensure-NativeConfigured([bool] $Release) {
    $expected = if ($Release) { "ON" } else { "OFF" }
    # CMake's build step handles changed CMakeLists automatically. Reconfigure
    # only on first use, after switching build/release modes, or for packaging
    # (which must refresh the source-asset integrity manifest).
    if ((Get-CMakeCacheValue "FVD_NATIVE_RELEASE") -ne $expected -or $Release) {
        Invoke-Checked "cmake.exe" @("-S", $root, "-B", $build,
            "-DFVD_NATIVE_RELEASE=$expected") "Configure native FVD ($expected)"
    }
}

function Get-LocalExecutable([string] $Name) {
    foreach ($candidate in @(
        (Join-Path $build "packaging\\RelWithDebInfo\\$Name"),
        (Join-Path $build "packaging\\$Name"))) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    throw "Native build did not produce $Name under $build\\packaging"
}

function Set-DeveloperQtEnvironment {
    # Use the already-installed vcpkg developer Qt runtime in-place, without
    # running windeployqt, extracting a package or copying the Qt distribution.
    $installed = Get-CMakeCacheValue "_VCPKG_INSTALLED_DIR"
    $triplet = Get-CMakeCacheValue "VCPKG_TARGET_TRIPLET"
    if (-not $installed -or -not $triplet) { return }
    $vcpkg = Join-Path $installed $triplet
    $qt = Join-Path $vcpkg "Qt6"
    if (Test-Path -LiteralPath (Join-Path $vcpkg "bin") -PathType Container) {
        $env:PATH = "$(Join-Path $vcpkg 'bin');$env:PATH"
    }
    if (Test-Path -LiteralPath (Join-Path $qt "plugins") -PathType Container) {
        $env:QT_PLUGIN_PATH = Join-Path $qt "plugins"
    }
    if (Test-Path -LiteralPath (Join-Path $qt "qml") -PathType Container) {
        $env:QML_IMPORT_PATH = Join-Path $qt "qml"
        $env:QML2_IMPORT_PATH = $env:QML_IMPORT_PATH
    }
    if (Test-Path -LiteralPath (Join-Path $vcpkg "etc\\fonts") -PathType Container) {
        $env:FONTCONFIG_PATH = Join-Path $vcpkg "etc\\fonts"
    }
}

Push-Location $root
try {
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
    # The source API must be present for every native build, not just release.
    foreach ($required in @("modpack.toml", "manifest.toml", "root.nyan")) {
        if (-not (Test-Path -LiteralPath (Join-Path $api $required) -PathType Leaf)) {
            throw "Tracked FVD engine API incomplete: missing $required under $api"
        }
    }

    Ensure-NativeConfigured ($Mode -eq "package")

    if ($Mode -eq "test") {
        $targets = @("openage_native_game_dev_runtime", "openage_native_relay",
            "openage_native_game_args_test", "openage_native_relay_args_test",
            "openage_native_relay_network_test", "openage_native_cpp_tests")
    }
    elseif ($Mode -in @("solo", "game")) {
        $targets = @("openage_native_game_dev_runtime")
    }
    elseif ($Mode -eq "relay") {
        $targets = @("openage_native_relay")
    }
    else {
        $targets = @("openage_native_game", "openage_native_relay")
    }
    Invoke-Checked "cmake.exe" (@("--build", $build, "--config", "RelWithDebInfo",
        "--target") + $targets + @("--parallel", "$Jobs")) "Incremental native $Mode build"

    if ($Mode -in @("game", "solo", "relay")) {
        $name = if ($Mode -eq "relay") { "openage-relay.exe" } else { "openage-native.exe" }
        $exe = Get-LocalExecutable $name
        $runArgs = @()
        if ($Mode -ne "relay") {
            Set-DeveloperQtEnvironment
            # Source assets are authoritative in developer mode. Explicit user
            # --asset-dir options can still override this default.
            $runArgs += @("--asset-dir", (Join-Path $root "assets"))
        }
        if ($Mode -eq "solo") { $runArgs += "--solo" }
        $runArgs += $NativeArgs
        Write-Host "==> Running freshly built $name (no archive)" -ForegroundColor Cyan
        & $exe @runArgs
        if ($LASTEXITCODE -ne 0) { throw "$Mode exited with code $LASTEXITCODE" }
        return
    }

    if ($Mode -eq "test") {
        Invoke-Checked "ctest.exe" @("--test-dir", $build, "-C", "RelWithDebInfo",
            "-R", "^fvd_native_(game|relay)_", "--output-on-failure") "Run native CTest suite"
        if ($NativeArgs.Count -gt 0) {
            $nativeRunner = Get-LocalExecutable "openage_native_cpp_tests.exe"
            Invoke-Checked $nativeRunner @($NativeArgs) "Run selected native C++ tests"
        }
        Write-Host "FVD native tests passed." -ForegroundColor Green
        return
    }

    if ($Mode -eq "build") {
        Write-Host "FVD local native game and relay are up to date." -ForegroundColor Green
        return
    }

    # Packaging is deliberately separate. No developer mode depends on
    # archives, and packaging does not unpack or launch the release archive.
    if (-not (Get-Command "cpack.exe" -ErrorAction SilentlyContinue)) {
        throw "CPack not found; it is installed with CMake."
    }
    New-Item -ItemType Directory -Path $dist -Force | Out-Null
    Invoke-Checked "cpack.exe" @("--config", (Join-Path $build "CPackConfig.cmake"),
        "-C", "RelWithDebInfo", "-G", "7Z", "-B", $dist) "Create native release 7Z"
    $archive = Get-ChildItem -LiteralPath $dist -Filter "*.7z" -File |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if (-not $archive) { throw "CPack finished but no 7Z archive was found in $dist" }
    Write-Host ""
    Write-Host "Release archive: $($archive.FullName)" -ForegroundColor Green
    Write-Host "Archive extraction and release smoke tests are separate manual deployment/acceptance steps."
}
finally {
    Pop-Location
}
