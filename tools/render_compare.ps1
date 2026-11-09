<#
.SYNOPSIS
    tools/render_compare.ps1 - Multi-scene screenshot capture and A/B comparison rig for Windows.
.DESCRIPTION
    Automates OpenEden / OpenMW rendering test passes across defined scenes.
.EXAMPLE
    .\tools\render_compare.ps1 capture -Binary .\build-jolt\openmw.exe -Scenes .\tools\render_scenes.txt -OutputDir .\out_ref
    .\tools\render_compare.ps1 compare -Dir1 .\out_ref -Dir2 .\out_vsg -Threshold 2.0
#>

[CmdletBinding()]
param(
    [Parameter(Position = 0, Mandatory = $true)]
    [ValidateSet("capture", "compare", "benchmark")]
    [string]$Mode,

    [Parameter(Position = 1)]
    [string]$Binary,

    [Parameter(Position = 2)]
    [string]$ScenesFile = "tools/render_scenes.txt",

    [Parameter(Position = 3)]
    [string]$OutputDir = "screenshots_captured",

    [string]$BaseConfigDir = "",
    [string]$Dir1 = "",
    [string]$Dir2 = "",
    [double]$Threshold = 2.0
)

$ErrorActionPreference = "Stop"

function Setup-IsolatedEnv {
    param([string]$BaseCfg, [string]$WorkDir)

    $cfgDir = Join-Path $WorkDir "config"
    $dataDir = Join-Path $WorkDir "userdata"
    $shotDir = Join-Path $dataDir "screenshots"

    if (Test-Path $WorkDir) { Remove-Item -Recurse -Force $WorkDir }
    New-Item -ItemType Directory -Path $cfgDir -Force | Out-Null
    New-Item -ItemType Directory -Path $shotDir -Force | Out-Null

    if ($BaseCfg -and (Test-Path (Join-Path $BaseCfg "openmw.cfg"))) {
        Copy-Item (Join-Path $BaseCfg "openmw.cfg") (Join-Path $cfgDir "openmw.cfg") -Force
    } elseif ($env:USERPROFILE -and (Test-Path "$env:USERPROFILE\Documents\My Games\OpenMW\openmw.cfg")) {
        Copy-Item "$env:USERPROFILE\Documents\My Games\OpenMW\openmw.cfg" (Join-Path $cfgDir "openmw.cfg") -Force
    }

    $settings = @"
[Video]
resolution x = 1280
resolution y = 720
fullscreen = false
window border = false
framerate limit = 0
vsync = false

[Shadows]
enable shadows = false
"@
    Set-Content -Path (Join-Path $cfgDir "settings.cfg") -Value $settings
}

function Invoke-Capture {
    if (-not $Binary -or -not (Test-Path $Binary)) {
        Write-Error "Binary path missing or invalid: $Binary"
        return
    }
    if (-not (Test-Path $ScenesFile)) {
        Write-Error "Scenes file missing: $ScenesFile"
        return
    }

    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
    $tmpDir = Join-Path $env:TEMP ("openmw_capture_" + [System.Guid]::NewGuid().ToString("N"))
    Write-Host "=== Running screenshot capture with binary: $Binary ==="
    Write-Host "Scenes file: $ScenesFile"
    Write-Host "Output dir:  $OutputDir"

    Get-Content $ScenesFile | ForEach-Object {
        $line = $_.Trim()
        if (-not $line -or $line.StartsWith("#")) { return }
        $parts = $line.Split('|')
        if ($parts.Length -lt 5) { return }

        $sceneName   = $parts[0]
        $sceneType   = $parts[1]
        $sceneTarget = $parts[2]
        $shotFrame   = [int]$parts[3]
        $quitFrame   = [int]$parts[4]
        $desc        = if ($parts.Length -ge 6) { $parts[5] } else { "" }

        Write-Host "--- Capturing scene: $sceneName ($desc) ---"
        Setup-IsolatedEnv -BaseCfg $BaseConfigDir -WorkDir $tmpDir

        $cfgDir = Join-Path $tmpDir "config"
        $dataDir = Join-Path $tmpDir "userdata"
        $shotDir = Join-Path $dataDir "screenshots"

        $targetArgs = @()
        if ($sceneType -eq "start") {
            # Start-Process joins the list without quoting, so a cell name with a space ("Sadrith Mora") must carry its own quotes.
            $targetArgs = @("--skip-menu", "--start", "`"$sceneTarget`"")
        } elseif ($sceneType -eq "save") {
            $targetArgs = @("--skip-menu", "--load-savegame", "`"$sceneTarget`"")
        }

        $allArgs = @(
            "--config", $cfgDir,
            "--user-data", $dataDir,
            "--replace=config"
        ) + $targetArgs + @(
            "--auto-screenshot=$shotFrame",
            "--auto-quit=$quitFrame"
        )

        $proc = Start-Process -FilePath $Binary -ArgumentList $allArgs -PassThru -NoNewWindow
        $finished = $proc.WaitForExit(35000)
        if (-not $finished) {
            Write-Warning "Process timed out after 35s. Terminating..."
            $proc.Kill()
        }

        $shots = Get-ChildItem -Path $shotDir -Filter "*.png" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending
        if ($shots -and $shots.Count -gt 0) {
            $dest = Join-Path $OutputDir "$sceneName.png"
            Copy-Item $shots[0].FullName $dest -Force
            Write-Host "  Saved: $dest"
        } else {
            Write-Warning "  No screenshot captured for scene: $sceneName"
        }
    }

    if (Test-Path $tmpDir) { Remove-Item -Recurse -Force $tmpDir }
    Write-Host "=== Capture complete ==="
}

function Invoke-Compare {
    $d1 = if ($Dir1) { $Dir1 } else { $Binary }
    $d2 = if ($Dir2) { $Dir2 } else { $ScenesFile }
    if (-not (Test-Path $d1) -or -not (Test-Path $d2)) {
        Write-Error "Both Dir1 ($d1) and Dir2 ($d2) must exist for comparison."
        return
    }

    Add-Type -AssemblyName System.Drawing
    $files1 = Get-ChildItem -Path $d1 -Filter "*.png" | ForEach-Object { $_.Name }
    $files2 = Get-ChildItem -Path $d2 -Filter "*.png" | ForEach-Object { $_.Name }
    $common = $files1 | Where-Object { $files2 -contains $_ } | Sort-Object

    if (-not $common -or $common.Count -eq 0) {
        Write-Error "No common PNG images found between $d1 and $d2."
        return
    }

    Write-Host "=== Comparing $($common.Count) scenes (Threshold MAE: $Threshold) ==="
    $allPassed = $true

    foreach ($fname in $common) {
        $p1 = Join-Path $d1 $fname
        $p2 = Join-Path $d2 $fname

        $bmp1 = [System.Drawing.Bitmap]::FromFile($p1)
        $bmp2 = [System.Drawing.Bitmap]::FromFile($p2)

        try {
            if ($bmp1.Width -ne $bmp2.Width -or $bmp1.Height -ne $bmp2.Height) {
                Write-Host "  [FAIL] $($fname): shape mismatch ($($bmp1.Width)x$($bmp1.Height) vs $($bmp2.Width)x$($bmp2.Height))" -ForegroundColor Red
                $allPassed = $false
                continue
            }

            $rect = [System.Drawing.Rectangle]::new(0, 0, $bmp1.Width, $bmp1.Height)
            $format = [System.Drawing.Imaging.PixelFormat]::Format24bppRgb

            $data1 = $bmp1.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, $format)
            $data2 = $bmp2.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, $format)

            $bytes = [Math]::Abs($data1.Stride) * $bmp1.Height
            $buf1 = [byte[]]::new($bytes)
            $buf2 = [byte[]]::new($bytes)

            [System.Runtime.InteropServices.Marshal]::Copy($data1.Scan0, $buf1, 0, $bytes)
            [System.Runtime.InteropServices.Marshal]::Copy($data2.Scan0, $buf2, 0, $bytes)

            $bmp1.UnlockBits($data1)
            $bmp2.UnlockBits($data2)

            [double]$totalDiff = 0
            [byte]$maxDiff = 0
            $pixelCount = $bmp1.Width * $bmp1.Height * 3

            for ($i = 0; $i -lt $bytes; $i++) {
                $d = [Math]::Abs([int]$buf1[$i] - [int]$buf2[$i])
                $totalDiff += $d
                if ($d -gt $maxDiff) { $maxDiff = [byte]$d }
            }

            $mae = $totalDiff / $pixelCount
            $passed = ($mae -le $Threshold)
            if (-not $passed) { $allPassed = $false }

            $statusStr = if ($passed) { "PASS" } else { "DIFF" }
            $color = if ($passed) { "Green" } else { "Yellow" }
            Write-Host ("  [{0}] {1,-30} MAE={2,6:N2} / 255  MaxChannelDiff={3,3}" -f $statusStr, $fname, $mae, $maxDiff) -ForegroundColor $color
        } finally {
            $bmp1.Dispose()
            $bmp2.Dispose()
        }
    }

    if (-not $allPassed) {
        Write-Warning "Visual differences exceed threshold."
        exit 2
    } else {
        Write-Host "All scenes passed visual comparison within threshold." -ForegroundColor Green
    }
}

if ($Mode -eq "capture") {
    Invoke-Capture
} elseif ($Mode -eq "compare") {
    Invoke-Compare
} elseif ($Mode -eq "benchmark") {
    Write-Host "Benchmark mode reserved for performance profile passes."
}
