# Copyright 2026 the openage authors. See copying.md for legal info.
# Native Windows Qt packaging fallback. Requires only PowerShell and Qt tools.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $Scanner,
    [Parameter(Mandatory = $true)][string] $QmlRoot,
    [Parameter(Mandatory = $true)][string] $QtRoot,
    [Parameter(Mandatory = $true)][string] $Destination
)
$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $Scanner -PathType Leaf)) {
    throw "Qt import scanner not found: $Scanner"
}
$qml = Join-Path $QtRoot "qml"
$plugins = Join-Path $QtRoot "plugins"
foreach ($dir in @($QmlRoot, $qml, $plugins)) {
    if (-not (Test-Path -LiteralPath $dir -PathType Container)) {
        throw "Missing required native Qt directory: $dir"
    }
}
$result = & $Scanner -rootPath $QmlRoot -importPath $qml
if ($LASTEXITCODE -ne 0) { throw "qmlimportscanner exit $LASTEXITCODE" }
try {
    $modules = @(($result -join [Environment]::NewLine) | ConvertFrom-Json)
}
catch {
    throw "Invalid Qt scanner JSON: $_"
}
if ($modules.Count -eq 0) { throw "Qt scanner returned no QML imports" }
$selected = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
foreach ($mod in $modules) {
    if ($mod.path -and $mod.relativePath) {
        [void] $selected.Add([IO.Path]::GetFullPath([string] $mod.path).TrimEnd('\', '/'))
    }
}
function Copy-QtTree([string] $Source, [string] $Target, [bool] $SkipNested) {
    $root = [IO.Path]::GetFullPath($Source).TrimEnd('\', '/')
    if (-not (Test-Path -LiteralPath $root -PathType Container)) {
        throw "Qt module or plugin directory missing: $root"
    }
    foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File) {
        if ($file.Extension -ieq ".pdb") { continue }
        if ($SkipNested) {
            $folder = $file.Directory.FullName
            $skip = $false
            while ($folder.Length -gt $root.Length) {
                if ((Test-Path -LiteralPath (Join-Path $folder "qmldir") -PathType Leaf) -and
                    -not $selected.Contains($folder)) {
                    $skip = $true
                    break
                }
                $folder = Split-Path -Parent $folder
            }
            if ($skip) { continue }
        }
        $relative = $file.FullName.Substring($root.Length).TrimStart('\', '/')
        $to = Join-Path $Target $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $to) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $to -Force
    }
}
foreach ($mod in $modules) {
    if (-not $mod.path -or -not $mod.relativePath) { continue }
    $relative = ([string] $mod.relativePath).Replace('/', '\')
    if ([IO.Path]::IsPathRooted($relative) -or
        ($relative -split '[\\/]') -contains '..') {
        throw "Unsafe Qt relative module path: $relative"
    }
    Copy-QtTree ([string] $mod.path) (Join-Path (Join-Path $Destination "qml") $relative) $true
}
Copy-QtTree $plugins (Join-Path $Destination "plugins") $false
Write-Host "PASS: native Qt modules and plugins deployed without Python"
