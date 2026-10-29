<#
.SYNOPSIS
    tools/verify_no_osg_deps.ps1 - Verify built executable has NO OSG/OpenThreads dependencies.

.DESCRIPTION
    Inspects built openmw executable using Python verifier or dumpbin /dependents on Windows.
    Fails with non-zero exit code if any osg*.dll or OpenThreads*.dll are linked.

.EXAMPLE
    .\tools\verify_no_osg_deps.ps1 -Binary .\build-jolt\openmw.exe
#>

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Binary = ""
)

$ErrorActionPreference = "Stop"

# Auto-detect binary if not provided
if (-not $Binary) {
    $searchPaths = @(
        "build-jolt\openmw.exe",
        "build\openmw.exe",
        "bin\openmw.exe",
        ".\openmw.exe"
    )
    foreach ($path in $searchPaths) {
        if (Test-Path $path) {
            $Binary = $path
            break
        }
    }
}

if (-not $Binary -or -not (Test-Path $Binary)) {
    Write-Error "Binary not found: $Binary"
    exit 2
}

# Resolve python
$pythonBin = ""
$candidates = @(
    "C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe",
    "python",
    "python3",
    "py"
)
foreach ($c in $candidates) {
    if (Test-Path $c) {
        $pythonBin = $c
        break
    }
    try {
        $testCmd = Get-Command $c -ErrorAction SilentlyContinue
        if ($testCmd) {
            $testOut = & $c --version 2>&1
            if ($LASTEXITCODE -eq 0) {
                $pythonBin = $c
                break
            }
        }
    } catch {}
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$pyScript = Join-Path $scriptDir "verify_no_osg_deps.py"

if (Test-Path $pyScript) {
    & $pythonBin $pyScript $Binary
    exit $LASTEXITCODE
} else {
    Write-Error "Verification script not found: $pyScript"
    exit 1
}
