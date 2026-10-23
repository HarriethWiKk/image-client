# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 HarriethWiKk
#
# qmllint over the QML module. The source tree has no qmldir (the build generates one), and
# without it qmllint cannot tell that Theme/StatusMap are singletons -- every Theme.* access
# then reports a missing property. Stage the sources with a qmldir derived from the build's
# manifest so lint sees the same module the app loads.
#
# [unqualified] is filtered out on purpose: the controllers are context properties
# (Jobs, Profiles, Settings, ...) which qmllint cannot type. Drop the filter once they are
# registered as QML singletons.

$ErrorActionPreference = 'Stop'

if (-not $env:QTDIR) { throw 'QTDIR is not set (Qt prefix, e.g. C:\Qt\6.8.3\msvc2022_64)' }
$env:PATH = (Join-Path $env:QTDIR 'bin') + ';' + $env:PATH

$root = Split-Path -Parent $PSScriptRoot
$manifest = Join-Path $root 'build\src\gui\App\qmldir'
if (-not (Test-Path $manifest)) { throw "missing $manifest - run scripts\build.cmd first" }

$stage = Join-Path ([IO.Path]::GetTempPath()) ('oic-lint-' + [guid]::NewGuid().ToString('N'))
$qmlStage = Join-Path $stage 'qml'
New-Item -ItemType Directory -Path $qmlStage -Force | Out-Null

try {
    Copy-Item (Join-Path $root 'src\gui\qml\*.qml') $qmlStage

    # Rebase the generated manifest into the staged qml/ directory: keep only type lines and
    # strip the qml/ path prefix (module/typeinfo/prefer do not describe types).
    $types = Get-Content $manifest |
        Where-Object { $_ -match '^\s*(singleton\s+)?[A-Za-z_][A-Za-z0-9_]*\s+\d+\.\d+\s+\S+\.qml\s*$' } |
        ForEach-Object { $_ -replace 'qml/', '' }
    @('module App') + $types | Set-Content (Join-Path $qmlStage 'qmldir') -Encoding utf8

    $files = Get-ChildItem $qmlStage -Filter *.qml | ForEach-Object { $_.FullName }
    $output = & qmllint -I $stage -I (Join-Path $env:QTDIR 'qml') $files 2>&1
    $exit = $LASTEXITCODE

    $actionable = $output | Where-Object { $_ -match '^(Warning|Error)' -and $_ -notmatch '\[unqualified\]' }
    if ($actionable) {
        $actionable | ForEach-Object { Write-Host $_ }
        exit 1
    }
    if ($exit -ne 0) {
        Write-Host "qmllint: no actionable warnings ($($output.Count) lines filtered)"
    } else {
        Write-Host 'qmllint: clean'
    }
}
finally {
    Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
}
