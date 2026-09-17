# =====================================================================
#  A2DPWB Launcher  -  build / deploy / run in one step (PowerShell)
#
#  Usage:
#    .\start.ps1                    # run with saved config / defaults
#    .\start.ps1 -Build             # compile first, then run
#    .\start.ps1 -Stop              # stop a running instance
#    .\start.ps1 -Codec sbc -Quality hq -Device 78:C1:1D:A7:BC:EE
#    .\start.ps1 -List              # list paired BT audio devices
#    .\start.ps1 -Tail              # keep console attached to the log
#
#  Optional config file: config.json next to this script. Simple keys:
#    { "device": "78:C1:1D:A7:BC:EE", "codec": "ssc", "quality": "hq" }
#    (also: "captureMode": "loopback|virtual", "audioDevice": "...", "usbPath": "...")
#  Command-line flags always override config.json.
# =====================================================================
param(
    [switch]$Build,
    [switch]$Stop,
    [switch]$List,
    [switch]$Tail,
    [string]$Codec,      # ssc | ldac | aptxhd | aptxll | aac | sbc | auto
    [string]$Quality,    # hq | sq | mq
    [string]$Device,     # XX:XX:XX:XX:XX:XX
    [string]$CaptureMode, # loopback | virtual
    [string]$AudioDevice, # exact IMMDevice friendly name
    [string]$UsbPath      # USB adapter path hint
)

$ErrorActionPreference = 'Stop'

$Repo      = 'C:\Projects\SSCOnWindows'
$BuildDir  = Join-Path $Repo 'build_msvc'
$BuildExe  = Join-Path $BuildDir 'app\Release\SSCOnWindows-0.1.exe'
$DistDir   = 'C:\Projects\SSCOnWindows\dist'
$DistExe   = Join-Path $DistDir 'SSCOnWindows-0.1.exe'
$OutLog    = Join-Path $env:TEMP 'a2dpwb_out.txt'
$ErrLog    = Join-Path $env:TEMP 'a2dpwb_err.txt'
$ConfigFile = Join-Path $PSScriptRoot 'config.json'

# ---- defaults (overridable by config.json, then by CLI flags) --------
$cfg = @{ device = '78:C1:1D:A7:BC:EE'; codec = 'ssc'; quality = 'hq' }
if (Test-Path $ConfigFile) {
    try {
        $fileCfg = Get-Content $ConfigFile -Raw | ConvertFrom-Json
        $cfg.device  = $fileCfg.device
        $cfg.codec   = $fileCfg.codec
        $cfg.quality = $fileCfg.quality
        if ($fileCfg.captureMode) { $cfg.captureMode = $fileCfg.captureMode }
        if ($fileCfg.audioDevice) { $cfg.audioDevice = $fileCfg.audioDevice }
        if ($fileCfg.usbPath)     { $cfg.usbPath = $fileCfg.usbPath }
        Write-Host "Loaded config from $ConfigFile"
    } catch { Write-Warning "Could not read config.json: $($_.Exception.Message)" }
}
# ---- CLI flags override config ---- 
if ($Codec)       { $cfg.codec = $Codec }
if ($Quality)     { $cfg.quality = $Quality }
if ($Device)      { $cfg.device = $Device }
if ($CaptureMode) { $cfg.captureMode = $CaptureMode }
if ($AudioDevice) { $cfg.audioDevice = $AudioDevice }
if ($UsbPath)     { $cfg.usbPath = $UsbPath }

function Stop-Instance {
    Get-Process | Where-Object { $_.Name -like 'SSCOnWindows*' -or $_.Name -like 'A2DPWB*' } | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep 1
    Write-Host 'Stopped any running SSC On Windows instance.'
}

if ($Stop) {
    Stop-Instance
    return
}

if ($List) {
    & $DistExe --cli -l
    return
}

if ($Build) {
    Write-Host 'Building (Release)...'
    cmake --build $BuildDir --config Release --target A2DPWB -j 8
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    Write-Host 'Build OK.'
}

if (-not (Test-Path $BuildExe)) {
    throw "Build output not found: $BuildExe  (run with -Build first)"
}

# ---- stop existing instance, then deploy ------------------------------
Stop-Instance

Write-Host 'Deploying to dist...'
Copy-Item $BuildExe $DistExe -Force
if (-not (Test-Path $DistExe)) { throw 'Deploy failed.' }
Write-Host "Deployed $DistExe"

# ---- assemble args -----------------------------------------------------
$args = @('--cli')
if ($cfg.device)       { $args += @('-d', $cfg.device) }
if ($cfg.codec)        { $args += @('-c', $cfg.codec) }
if ($cfg.quality)      { $args += @('-q', $cfg.quality) }
if ($cfg.captureMode)  { $args += @('-m', $cfg.captureMode) }
if ($cfg.audioDevice)  { $args += @('--audio-device', $cfg.audioDevice) }
if ($cfg.usbPath)      { $args += @('-u', $cfg.usbPath) }

$qArgs = $args | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }
$inner = '"' + $DistExe + '" ' + ($qArgs -join ' ') +
         ' > "' + $OutLog + '" 2> "' + $ErrLog + '"'
$full  = 'cmd /c start "" /b cmd /c ' + $inner

Write-Host ('Running: ' + (('"' + $DistExe + '" ' + ($qArgs -join ' '))) )
Remove-Item $OutLog, $ErrLog -Force -ErrorAction SilentlyContinue
[System.Diagnostics.Process]::Start('cmd.exe', '/c ' + $full) | Out-Null

Start-Sleep 3
if ($Tail) {
    Write-Host "--- tailing $ErrLog (Ctrl+C to detach) ---"
    Get-Content $ErrLog -Wait -ErrorAction SilentlyContinue
} else {
    # Open a live stats viewer window (the exe is GUI-subsystem, so it logs
    # to the file; this window tails it for live stats).
    $viewCmd = 'powershell -NoProfile -WindowStyle Normal -Command "Write-Host ''SSC On Windows live stats (Ctrl+C to close)''; Get-Content ' + $ErrLog + ' -Wait"'
    Start-Process 'cmd.exe' -ArgumentList @('/k', $viewCmd)
    Write-Host "Started. Logs:"
    Write-Host "  out: $OutLog"
    Write-Host "  err: $ErrLog"
    Write-Host "A live stats window has been opened."
}