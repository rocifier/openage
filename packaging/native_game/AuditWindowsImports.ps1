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

$pythonFiles = @(Get-ChildItem -LiteralPath $stageRoot -File -Recurse -Force |
    Where-Object {
        $_.Extension -match '^\.(py|pyc|pyo|pyd|whl|pth)
if ($pythonFiles.Count -gt 0) {
    throw "Python runtime/developer files found: $($pythonFiles.FullName -join ', ')"
}

# A Python runtime can be smuggled into a package without any .py files.
# Reject interpreter directories as well as import-library and module artifacts.
$pythonDirectories = @(Get-ChildItem -LiteralPath $stageRoot -Directory -Recurse -Force |
    Where-Object { $_.Name -match '^(?i:__pycache__|site-packages|dist-packages|python[0-9][0-9._-]*)
    Where-Object { $_.Extension -match '^\.(exe|dll)$' })
foreach ($binary in $binaries) {
    $output = & $Dumpbin /DEPENDENTS $binary.FullName 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed for $($binary.FullName): $output"
    }
    $imports = [regex]::Matches($output, '(?im)^\s*([a-z0-9_.-]+\.dll)\s*$')
    foreach ($import in $imports) {
        $dependency = $import.Groups[1].Value
        if ($dependency -match '^(?i:(?:lib)?python\d*(?:_d)?)[.]dll
    }
}
Write-Host "FVDNative dependency audit passed: $($binaries.Count) Windows binaries; all non-system DLL imports resolved within stage; no Python imports/files."
 })
if ($pythonDirectories.Count -gt 0) {
    throw "Python runtime directories found: $($pythonDirectories.FullName -join ', ')"
}

# GitHub-hosted builders may have extra DLLs on PATH, but a clean machine
# will not. Every non-OS import must be present in the extracted archive.
# The EXE directory is the DLL loading root even when Qt plugins live deeper.
$binRoot = Join-Path $stageRoot "bin"
$systemRoot = Join-Path $env:WINDIR "System32"
if (-not (Test-Path -LiteralPath $systemRoot -PathType Container)) {
    throw "Cannot locate Windows System32 for native dependency audit"
}

function Test-PackagedDependency([string] $Name, [string] $BinaryDirectory) {
    foreach ($directory in @($BinaryDirectory, $binRoot)) {
        if (Test-Path -LiteralPath (Join-Path $directory $Name) -PathType Leaf) {
            return $true
        }
    }
    if ($Name -match '^(?i:api-ms-win-|ext-ms-win-)') {
        # Windows API-set imports are resolved by the OS, not bundled as DLLs.
        return $true
    }
    if ($Name -match '^(?i:vcruntime|msvcp|concrt|vccorlib|msvcr)[0-9_]*[.]dll$') {
        # MSVC redistributables must be bundled, not borrowed from the build host.
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
    $imports = [regex]::Matches($output, '(?im)^\s*([a-z0-9_.-]+\.dll)\s*$')
    foreach ($import in $imports) {
        if ($import.Groups[1].Value -match '^(?:lib)?python\d*(?:_d)?\.dll$') {
            throw "$($binary.FullName) imports Python: $($import.Groups[1].Value)"
        }
    }
}
Write-Host "FVDNative dependency audit passed: $($binaries.Count) Windows binaries, no Python imports/files."
 -or
        $_.Name -match '^(lib)?python\d+(?:_d)?\.(dll|exe|lib|zip|_pth)
if ($pythonFiles.Count -gt 0) {
    throw "Python runtime/developer files found: $($pythonFiles.FullName -join ', ')"
}

# A Python runtime can be smuggled into a package without any .py files.
# Reject interpreter directories as well as import-library and module artifacts.
$pythonDirectories = @(Get-ChildItem -LiteralPath $stageRoot -Directory -Recurse -Force |
    Where-Object { $_.Name -match '^(?i:__pycache__|site-packages|dist-packages|python[0-9][0-9._-]*)
    Where-Object { $_.Extension -match '^\.(exe|dll)$' })
foreach ($binary in $binaries) {
    $output = & $Dumpbin /DEPENDENTS $binary.FullName 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed for $($binary.FullName): $output"
    }
    $imports = [regex]::Matches($output, '(?im)^\s*([a-z0-9_.-]+\.dll)\s*$')
    foreach ($import in $imports) {
        if ($import.Groups[1].Value -match '^(?:lib)?python\d*(?:_d)?\.dll$') {
            throw "$($binary.FullName) imports Python: $($import.Groups[1].Value)"
        }
    }
}
Write-Host "FVDNative dependency audit passed: $($binaries.Count) Windows binaries, no Python imports/files."
 })
if ($pythonDirectories.Count -gt 0) {
    throw "Python runtime directories found: $($pythonDirectories.FullName -join ', ')"
}

$binaries = @(Get-ChildItem -LiteralPath $stageRoot -File -Recurse -Force |
    Where-Object { $_.Extension -match '^\.(exe|dll)$' })
foreach ($binary in $binaries) {
    $output = & $Dumpbin /DEPENDENTS $binary.FullName 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed for $($binary.FullName): $output"
    }
    $imports = [regex]::Matches($output, '(?im)^\s*([a-z0-9_.-]+\.dll)\s*$')
    foreach ($import in $imports) {
        if ($import.Groups[1].Value -match '^(?:lib)?python\d*(?:_d)?\.dll$') {
            throw "$($binary.FullName) imports Python: $($import.Groups[1].Value)"
        }
    }
}
Write-Host "FVDNative dependency audit passed: $($binaries.Count) Windows binaries, no Python imports/files."
 -or
        $_.Name -match '^pythonw?\.exe
if ($pythonFiles.Count -gt 0) {
    throw "Python runtime/developer files found: $($pythonFiles.FullName -join ', ')"
}

# A Python runtime can be smuggled into a package without any .py files.
# Reject interpreter directories as well as import-library and module artifacts.
$pythonDirectories = @(Get-ChildItem -LiteralPath $stageRoot -Directory -Recurse -Force |
    Where-Object { $_.Name -match '^(?i:__pycache__|site-packages|dist-packages|python[0-9][0-9._-]*)
    Where-Object { $_.Extension -match '^\.(exe|dll)$' })
foreach ($binary in $binaries) {
    $output = & $Dumpbin /DEPENDENTS $binary.FullName 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed for $($binary.FullName): $output"
    }
    $imports = [regex]::Matches($output, '(?im)^\s*([a-z0-9_.-]+\.dll)\s*$')
    foreach ($import in $imports) {
        if ($import.Groups[1].Value -match '^(?:lib)?python\d*(?:_d)?\.dll$') {
            throw "$($binary.FullName) imports Python: $($import.Groups[1].Value)"
        }
    }
}
Write-Host "FVDNative dependency audit passed: $($binaries.Count) Windows binaries, no Python imports/files."
 })
if ($pythonDirectories.Count -gt 0) {
    throw "Python runtime directories found: $($pythonDirectories.FullName -join ', ')"
}

$binaries = @(Get-ChildItem -LiteralPath $stageRoot -File -Recurse -Force |
    Where-Object { $_.Extension -match '^\.(exe|dll)$' })
foreach ($binary in $binaries) {
    $output = & $Dumpbin /DEPENDENTS $binary.FullName 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed for $($binary.FullName): $output"
    }
    $imports = [regex]::Matches($output, '(?im)^\s*([a-z0-9_.-]+\.dll)\s*$')
    foreach ($import in $imports) {
        if ($import.Groups[1].Value -match '^(?:lib)?python\d*(?:_d)?\.dll$') {
            throw "$($binary.FullName) imports Python: $($import.Groups[1].Value)"
        }
    }
}
Write-Host "FVDNative dependency audit passed: $($binaries.Count) Windows binaries, no Python imports/files."

    })
if ($pythonFiles.Count -gt 0) {
    throw "Python runtime/developer files found: $($pythonFiles.FullName -join ', ')"
}

# A Python runtime can be smuggled into a package without any .py files.
# Reject interpreter directories as well as import-library and module artifacts.
$pythonDirectories = @(Get-ChildItem -LiteralPath $stageRoot -Directory -Recurse -Force |
    Where-Object { $_.Name -match '^(?i:__pycache__|site-packages|dist-packages|python[0-9][0-9._-]*)
    Where-Object { $_.Extension -match '^\.(exe|dll)$' })
foreach ($binary in $binaries) {
    $output = & $Dumpbin /DEPENDENTS $binary.FullName 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed for $($binary.FullName): $output"
    }
    $imports = [regex]::Matches($output, '(?im)^\s*([a-z0-9_.-]+\.dll)\s*$')
    foreach ($import in $imports) {
        if ($import.Groups[1].Value -match '^(?:lib)?python\d*(?:_d)?\.dll$') {
            throw "$($binary.FullName) imports Python: $($import.Groups[1].Value)"
        }
    }
}
Write-Host "FVDNative dependency audit passed: $($binaries.Count) Windows binaries, no Python imports/files."
 })
if ($pythonDirectories.Count -gt 0) {
    throw "Python runtime directories found: $($pythonDirectories.FullName -join ', ')"
}

$binaries = @(Get-ChildItem -LiteralPath $stageRoot -File -Recurse -Force |
    Where-Object { $_.Extension -match '^\.(exe|dll)$' })
foreach ($binary in $binaries) {
    $output = & $Dumpbin /DEPENDENTS $binary.FullName 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed for $($binary.FullName): $output"
    }
    $imports = [regex]::Matches($output, '(?im)^\s*([a-z0-9_.-]+\.dll)\s*$')
    foreach ($import in $imports) {
        if ($import.Groups[1].Value -match '^(?:lib)?python\d*(?:_d)?\.dll$') {
            throw "$($binary.FullName) imports Python: $($import.Groups[1].Value)"
        }
    }
}
Write-Host "FVDNative dependency audit passed: $($binaries.Count) Windows binaries, no Python imports/files."
) {
            throw "$($binary.FullName) imports Python: $dependency"
        }
        if (-not (Test-PackagedDependency $dependency $binary.DirectoryName)) {
            throw "Missing non-system DLL dependency: $($binary.FullName) -> $dependency"
        }
    }
}
Write-Host "FVDNative dependency audit passed: $($binaries.Count) Windows binaries, no Python imports/files."
 })
if ($pythonDirectories.Count -gt 0) {
    throw "Python runtime directories found: $($pythonDirectories.FullName -join ', ')"
}

# GitHub-hosted builders may have extra DLLs on PATH, but a clean machine
# will not. Every non-OS import must be present in the extracted archive.
# The EXE directory is the DLL loading root even when Qt plugins live deeper.
$binRoot = Join-Path $stageRoot "bin"
$systemRoot = Join-Path $env:WINDIR "System32"
if (-not (Test-Path -LiteralPath $systemRoot -PathType Container)) {
    throw "Cannot locate Windows System32 for native dependency audit"
}

function Test-PackagedDependency([string] $Name, [string] $BinaryDirectory) {
    foreach ($directory in @($BinaryDirectory, $binRoot)) {
        if (Test-Path -LiteralPath (Join-Path $directory $Name) -PathType Leaf) {
            return $true
        }
    }
    if ($Name -match '^(?i:api-ms-win-|ext-ms-win-)') {
        # Windows API-set imports are resolved by the OS, not bundled as DLLs.
        return $true
    }
    if ($Name -match '^(?i:vcruntime|msvcp|concrt|vccorlib|msvcr)[0-9_]*[.]dll$') {
        # MSVC redistributables must be bundled, not borrowed from the build host.
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
    $imports = [regex]::Matches($output, '(?im)^\s*([a-z0-9_.-]+\.dll)\s*$')
    foreach ($import in $imports) {
        if ($import.Groups[1].Value -match '^(?:lib)?python\d*(?:_d)?\.dll$') {
            throw "$($binary.FullName) imports Python: $($import.Groups[1].Value)"
        }
    }
}
Write-Host "FVDNative dependency audit passed: $($binaries.Count) Windows binaries, no Python imports/files."
 -or
        $_.Name -match '^(lib)?python\d+(?:_d)?\.(dll|exe|lib|zip|_pth)
if ($pythonFiles.Count -gt 0) {
    throw "Python runtime/developer files found: $($pythonFiles.FullName -join ', ')"
}

# A Python runtime can be smuggled into a package without any .py files.
# Reject interpreter directories as well as import-library and module artifacts.
$pythonDirectories = @(Get-ChildItem -LiteralPath $stageRoot -Directory -Recurse -Force |
    Where-Object { $_.Name -match '^(?i:__pycache__|site-packages|dist-packages|python[0-9][0-9._-]*)
    Where-Object { $_.Extension -match '^\.(exe|dll)$' })
foreach ($binary in $binaries) {
    $output = & $Dumpbin /DEPENDENTS $binary.FullName 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed for $($binary.FullName): $output"
    }
    $imports = [regex]::Matches($output, '(?im)^\s*([a-z0-9_.-]+\.dll)\s*$')
    foreach ($import in $imports) {
        if ($import.Groups[1].Value -match '^(?:lib)?python\d*(?:_d)?\.dll$') {
            throw "$($binary.FullName) imports Python: $($import.Groups[1].Value)"
        }
    }
}
Write-Host "FVDNative dependency audit passed: $($binaries.Count) Windows binaries, no Python imports/files."
 })
if ($pythonDirectories.Count -gt 0) {
    throw "Python runtime directories found: $($pythonDirectories.FullName -join ', ')"
}

$binaries = @(Get-ChildItem -LiteralPath $stageRoot -File -Recurse -Force |
    Where-Object { $_.Extension -match '^\.(exe|dll)$' })
foreach ($binary in $binaries) {
    $output = & $Dumpbin /DEPENDENTS $binary.FullName 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed for $($binary.FullName): $output"
    }
    $imports = [regex]::Matches($output, '(?im)^\s*([a-z0-9_.-]+\.dll)\s*$')
    foreach ($import in $imports) {
        if ($import.Groups[1].Value -match '^(?:lib)?python\d*(?:_d)?\.dll$') {
            throw "$($binary.FullName) imports Python: $($import.Groups[1].Value)"
        }
    }
}
Write-Host "FVDNative dependency audit passed: $($binaries.Count) Windows binaries, no Python imports/files."
 -or
        $_.Name -match '^pythonw?\.exe
if ($pythonFiles.Count -gt 0) {
    throw "Python runtime/developer files found: $($pythonFiles.FullName -join ', ')"
}

# A Python runtime can be smuggled into a package without any .py files.
# Reject interpreter directories as well as import-library and module artifacts.
$pythonDirectories = @(Get-ChildItem -LiteralPath $stageRoot -Directory -Recurse -Force |
    Where-Object { $_.Name -match '^(?i:__pycache__|site-packages|dist-packages|python[0-9][0-9._-]*)
    Where-Object { $_.Extension -match '^\.(exe|dll)$' })
foreach ($binary in $binaries) {
    $output = & $Dumpbin /DEPENDENTS $binary.FullName 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed for $($binary.FullName): $output"
    }
    $imports = [regex]::Matches($output, '(?im)^\s*([a-z0-9_.-]+\.dll)\s*$')
    foreach ($import in $imports) {
        if ($import.Groups[1].Value -match '^(?:lib)?python\d*(?:_d)?\.dll$') {
            throw "$($binary.FullName) imports Python: $($import.Groups[1].Value)"
        }
    }
}
Write-Host "FVDNative dependency audit passed: $($binaries.Count) Windows binaries, no Python imports/files."
 })
if ($pythonDirectories.Count -gt 0) {
    throw "Python runtime directories found: $($pythonDirectories.FullName -join ', ')"
}

$binaries = @(Get-ChildItem -LiteralPath $stageRoot -File -Recurse -Force |
    Where-Object { $_.Extension -match '^\.(exe|dll)$' })
foreach ($binary in $binaries) {
    $output = & $Dumpbin /DEPENDENTS $binary.FullName 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed for $($binary.FullName): $output"
    }
    $imports = [regex]::Matches($output, '(?im)^\s*([a-z0-9_.-]+\.dll)\s*$')
    foreach ($import in $imports) {
        if ($import.Groups[1].Value -match '^(?:lib)?python\d*(?:_d)?\.dll$') {
            throw "$($binary.FullName) imports Python: $($import.Groups[1].Value)"
        }
    }
}
Write-Host "FVDNative dependency audit passed: $($binaries.Count) Windows binaries, no Python imports/files."

    })
if ($pythonFiles.Count -gt 0) {
    throw "Python runtime/developer files found: $($pythonFiles.FullName -join ', ')"
}

# A Python runtime can be smuggled into a package without any .py files.
# Reject interpreter directories as well as import-library and module artifacts.
$pythonDirectories = @(Get-ChildItem -LiteralPath $stageRoot -Directory -Recurse -Force |
    Where-Object { $_.Name -match '^(?i:__pycache__|site-packages|dist-packages|python[0-9][0-9._-]*)
    Where-Object { $_.Extension -match '^\.(exe|dll)$' })
foreach ($binary in $binaries) {
    $output = & $Dumpbin /DEPENDENTS $binary.FullName 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed for $($binary.FullName): $output"
    }
    $imports = [regex]::Matches($output, '(?im)^\s*([a-z0-9_.-]+\.dll)\s*$')
    foreach ($import in $imports) {
        if ($import.Groups[1].Value -match '^(?:lib)?python\d*(?:_d)?\.dll$') {
            throw "$($binary.FullName) imports Python: $($import.Groups[1].Value)"
        }
    }
}
Write-Host "FVDNative dependency audit passed: $($binaries.Count) Windows binaries, no Python imports/files."
 })
if ($pythonDirectories.Count -gt 0) {
    throw "Python runtime directories found: $($pythonDirectories.FullName -join ', ')"
}

$binaries = @(Get-ChildItem -LiteralPath $stageRoot -File -Recurse -Force |
    Where-Object { $_.Extension -match '^\.(exe|dll)$' })
foreach ($binary in $binaries) {
    $output = & $Dumpbin /DEPENDENTS $binary.FullName 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed for $($binary.FullName): $output"
    }
    $imports = [regex]::Matches($output, '(?im)^\s*([a-z0-9_.-]+\.dll)\s*$')
    foreach ($import in $imports) {
        if ($import.Groups[1].Value -match '^(?:lib)?python\d*(?:_d)?\.dll$') {
            throw "$($binary.FullName) imports Python: $($import.Groups[1].Value)"
        }
    }
}
Write-Host "FVDNative dependency audit passed: $($binaries.Count) Windows binaries, no Python imports/files."
