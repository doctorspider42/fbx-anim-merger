<#
.SYNOPSIS
    Packages a built tree into the two things a release ships: a portable zip and
    an Inno Setup installer.

.DESCRIPTION
    Both come out of the same binaries. The only difference is the PORTABLE marker
    file, which the zip carries and the installed copy does not - that is how the
    application decides whether it is allowed to update itself in place.

    Run it after a successful build:

        pwsh tools/package.ps1

    The version is derived exactly the way CMake derives it (VERSION plus the
    commit count), so a package built here carries the same number as the binaries
    inside it. Pass -Version to pin it, which is what CI does for tag builds.

.PARAMETER SkipInstaller
    Produce only the portable zip. Useful on a machine without Inno Setup.
#>
[CmdletBinding()]
param(
    [string]$BuildDir = "build",
    [string]$OutputDir = "dist",
    [string]$Version = "",
    [switch]$SkipInstaller
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Inno Setup is normally installed per-user these days, so %LOCALAPPDATA%\Programs
# comes first; the Program Files locations cover an older or machine-wide install,
# and $env:ISCC overrides the lot.
function Find-InnoSetupCompiler {
    if ($env:ISCC -and (Test-Path $env:ISCC)) { return $env:ISCC }

    $roots = @(
        (Join-Path $env:LOCALAPPDATA 'Programs'),
        ${env:ProgramFiles(x86)},
        $env:ProgramFiles
    )
    foreach ($root in $roots) {
        if ([string]::IsNullOrWhiteSpace($root)) { continue }
        foreach ($edition in @('Inno Setup 7', 'Inno Setup 6', 'Inno Setup 5')) {
            $candidate = Join-Path $root (Join-Path $edition 'ISCC.exe')
            if (Test-Path $candidate) { return $candidate }
        }
    }

    $onPath = Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    return $null
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot
try {
    # ----------------------------------------------------------------- version
    if ([string]::IsNullOrWhiteSpace($Version)) {
        $base = (Get-Content (Join-Path $repoRoot 'VERSION') -Raw).Trim()
        $count = '0'
        try {
            $count = (git rev-list --count HEAD 2>$null).Trim()
        } catch {
            $count = '0'
        }
        if ([string]::IsNullOrWhiteSpace($count)) { $count = '0' }
        $Version = "$base.$count"
    }
    if ($Version -notmatch '^\d+\.\d+\.\d+$') {
        throw "Version must be MAJOR.MINOR.PATCH, got '$Version'"
    }
    Write-Host "Packaging FBX Animation Merger $Version" -ForegroundColor Cyan

    # ------------------------------------------------------------ input files
    $binDir = Join-Path $repoRoot (Join-Path $BuildDir 'bin')
    $binaries = @('FbxAnimMerger.exe', 'fam-cli.exe')
    $documents = @('README.md', 'LICENSE', 'THIRD_PARTY_LICENSES.md')

    foreach ($name in $binaries) {
        $path = Join-Path $binDir $name
        if (-not (Test-Path $path)) {
            throw "Missing $path - build the project before packaging."
        }
    }

    $stageRoot = Join-Path $repoRoot (Join-Path $BuildDir 'package')
    if (Test-Path $stageRoot) { Remove-Item $stageRoot -Recurse -Force }

    $portableName = "FbxAnimMerger-$Version-windows-x64"
    $portableDir = Join-Path $stageRoot $portableName
    $appDir = Join-Path $stageRoot 'app'
    New-Item -ItemType Directory -Path $portableDir -Force | Out-Null
    New-Item -ItemType Directory -Path $appDir -Force | Out-Null

    foreach ($target in @($portableDir, $appDir)) {
        foreach ($name in $binaries) { Copy-Item (Join-Path $binDir $name) $target }
        foreach ($name in $documents) { Copy-Item (Join-Path $repoRoot $name) $target }
    }

    # The marker only exists in the portable copy. Updater::IsPortableBuild looks
    # for it next to the .exe and, finding it, offers a download rather than
    # trying to run an installer over a folder no installer owns.
    $markerText = @"
This copy is portable: unzip it anywhere, run FbxAnimMerger.exe, nothing is
written outside this folder.

Deleting this file makes the application believe it was installed and lets it
replace itself with the installer from GitHub. That is almost certainly not what
you want for a folder you unzipped by hand.

Version $Version
"@
    Set-Content -Path (Join-Path $portableDir 'PORTABLE') -Value $markerText -Encoding utf8

    $outDir = Join-Path $repoRoot $OutputDir
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null

    # ------------------------------------------------------------ portable zip
    $zipPath = Join-Path $outDir "$portableName-portable.zip"
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Compress-Archive -Path $portableDir -DestinationPath $zipPath -CompressionLevel Optimal
    Write-Host "  portable  $zipPath" -ForegroundColor Green

    # -------------------------------------------------------------- installer
    if ($SkipInstaller) {
        Write-Host "  installer skipped (-SkipInstaller)" -ForegroundColor Yellow
        return
    }

    $iscc = Find-InnoSetupCompiler
    if (-not $iscc) {
        throw @"
Inno Setup's compiler (ISCC.exe) was not found. Install it from
https://jrsoftware.org/isdl.php - the per-user install lands in
%LOCALAPPDATA%\Programs\Inno Setup 7, which is the first place this script looks.
Set ISCC to its full path to use a copy from somewhere else, or pass
-SkipInstaller to build only the portable zip.
"@
    }
    Write-Host "  using     $iscc"

    $script = Join-Path $repoRoot 'installer\FbxAnimMerger.iss'
    & $iscc "/DAppVersion=$Version" "/DPayloadDir=$appDir" "/DOutDir=$outDir" $script | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "ISCC failed with exit code $LASTEXITCODE"
    }

    $setupPath = Join-Path $outDir "FbxAnimMerger-$Version-setup.exe"
    if (-not (Test-Path $setupPath)) {
        throw "ISCC reported success but $setupPath is missing"
    }
    Write-Host "  installer $setupPath" -ForegroundColor Green
}
finally {
    Pop-Location
}
