# Copyright 2026 the openage authors. See copying.md for legal info.
# Mock the Qt QML scanner to validate fallback behavior without Qt installed.
$ErrorActionPreference = "Stop"
$temp = Join-Path ([IO.Path]::GetTempPath()) ("fvd-qt-test-" + [guid]::NewGuid().ToString("N"))
try {
    $qml = Join-Path $temp "assets"
    $qt = Join-Path $temp "Qt6"
    $module = Join-Path $qt "qml/QtQuick"
    $secondModule = Join-Path $qt "qml/QtQml"
    $nested = Join-Path $module "Controls"
    $plugins = Join-Path $qt "plugins/platforms"
    foreach ($dir in @($qml, $module, $secondModule, $nested, $plugins)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    Set-Content -LiteralPath (Join-Path $qml "main.qml") -Value "import QtQuick 2.15"
    Set-Content -LiteralPath (Join-Path $module "qmldir") -Value "module QtQuick"
    Set-Content -LiteralPath (Join-Path $module "main.qml") -Value "Item {}"
    Set-Content -LiteralPath (Join-Path $module "symbols.pdb") -Value "debug-only"
    Set-Content -LiteralPath (Join-Path $secondModule "qmldir") -Value "module QtQml"
    Set-Content -LiteralPath (Join-Path $nested "qmldir") -Value "module QtQuick.Controls"
    Set-Content -LiteralPath (Join-Path $nested "controls.qml") -Value "Button {}"
    Set-Content -LiteralPath (Join-Path $plugins "qwindows.dll") -Value "mock-plugin"
    $scanner = Join-Path $temp "mock-scanner.ps1"
    @'
param([string] $rootPath, [string] $importPath)
@(
    @{ path = $env:FVD_QT_TEST_MODULE; relativePath = $env:FVD_QT_TEST_RELATIVE },
    @{ path = $env:FVD_QT_TEST_SECOND_MODULE; relativePath = "QtQml" }
) |
    ConvertTo-Json -Compress
exit 0
'@ | Set-Content -LiteralPath $scanner -Encoding UTF8
    $env:FVD_QT_TEST_MODULE = $module
    $env:FVD_QT_TEST_SECOND_MODULE = $secondModule
    $env:FVD_QT_TEST_RELATIVE = "QtQuick"
    $dest = Join-Path $temp "stage/bin"
    & (Join-Path $PSScriptRoot "DeployQtWindows.ps1") -Scanner $scanner -QmlRoot $qml -QtRoot $qt -Destination $dest
    foreach ($expected in @(
        "qml/QtQuick/qmldir",
        "qml/QtQuick/main.qml",
        "qml/QtQml/qmldir",
        "plugins/platforms/qwindows.dll")) {
        if (-not (Test-Path -LiteralPath (Join-Path $dest $expected) -PathType Leaf)) {
            throw "Qt deployer omitted module/plugin $expected"
        }
    }
    foreach ($excluded in @("qml/QtQuick/Controls/controls.qml", "qml/QtQuick/symbols.pdb")) {
        if (Test-Path -LiteralPath (Join-Path $dest $excluded)) {
            throw "Qt deployer included unselected/debug file $excluded"
        }
    }
    $env:FVD_QT_TEST_RELATIVE = "../escaped"
    $rejected = $false
    try {
        & (Join-Path $PSScriptRoot "DeployQtWindows.ps1") -Scanner $scanner -QmlRoot $qml -QtRoot $qt -Destination $dest
    }
    catch {
        if ($_.Exception.Message -notmatch "Unsafe Qt relative") { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw "Qt deployer accepted path traversal" }
    Write-Host "PASS: Python-free Qt deployment and unsafe path rejection"
}
finally {
    Remove-Item Env:\FVD_QT_TEST_MODULE -ErrorAction SilentlyContinue
    Remove-Item Env:\FVD_QT_TEST_SECOND_MODULE -ErrorAction SilentlyContinue
    Remove-Item Env:\FVD_QT_TEST_RELATIVE -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $temp) {
        Remove-Item -LiteralPath $temp -Recurse -Force
    }
}
