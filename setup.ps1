# =====================================================================
#  setup.ps1  -  one-shot A2DPWB+SSB setup (Windows -> WSL2 daemon)
#
#  Automates the ~1 day manual SSC daemon bootstrap down to ~5 minutes:
#   1. Checks WSL2 is installed and a distro is available.
#   2. Deploys the vendored SSC payload (tools\ssc_payload\) into WSL
#      (blob + daemon source + helper + android libc shims).
#   3. Rewrites the daemon's hardcoded home paths to match the WSL user.
#   4. Compiles sscblobd with gcc inside WSL.
#   5. Verifies qemu-aarch64 + aarch64 sysroot (apt install when asked).
#   6. Ensures .wslconfig RAM/idle settings (Phase-1 tuning).
#   7. Reports the dongle driver mode (WinUSB=Streaming vs BTHUSB=Windows BT).
#   8. Optional -Smoke: runs the SSC golden-file check (229k) end-to-end.
#
#  Usage:
#    .\setup.ps1                        # one-shot: preflight -> deploy -> compile -> verify (idempotent)
#    .\setup.ps1 -BlobFrom <path|dir>   # use YOUR OWN legally-obtained blob (ELF-validated; recommended)
#    .\setup.ps1 -Distro Ubuntu         # target a specific WSL distro
#    .\setup.ps1 -SkipCompile           # deploy/paths only (no gcc)
#    .\setup.ps1 -SkipApt               # never propose apt installs
#    .\setup.ps1 -ForceWslConfig        # overwrite .wslconfig (backup kept)
#    .\setup.ps1 -Smoke                 # after setup: run the golden regression (needs blob + python)
#    .\setup.ps1 -Quiet                 # minimal output
#
#  Every stage is numbered (=== N. title ===) and re-runnable: re-running the
#  script is safe (deploys are idempotent, .wslconfig is merged, not clobbered).
#  Legal blob note: the repository currently vendors a proprietary Samsung
#  encoder blob. Prefer providing your own copy extracted from a Galaxy device
#  you own (e.g. from its system/vendor firmware image - exactly the reference
#  methodology used by sachk/openssc: locate libScalable_Encoder.so in
#  vendor/lib64 or the extracted com.android.bt image). Do NOT download blobs
#  from third parties.
#
#  Encoding note: wsl -l* prints UTF-16LE to files/blocks while `bash -lc`
#  output is UTF-8 (no BOM). We therefore write each bash script to a temp
#  .sh file and capture stdout via cmd redirection, then decode UTF-8.
# =====================================================================
param(
    [string]$Distro,
    [switch]$SkipCompile,
    [switch]$SkipApt,
    [switch]$Smoke,
    [switch]$ForceWslConfig,
    [switch]$Quiet,
    [string]$BlobFrom = '',
    [string]$Repo = 'C:\Projects\SSCOnWindows'
)

$ErrorActionPreference = 'Stop'
$PayloadDir = Join-Path $Repo 'tools\ssc_payload'
$WslConfigPath = Join-Path $env:USERPROFILE '.wslconfig'

function Log  { if (-not $Quiet) { Write-Host $args } }
function Die  { param([string]$Msg, [string]$Tip = '')
    Write-Host "ERROR: $Msg" -ForegroundColor Red
    if ($Tip) { Write-Host "  Fix: $Tip" -ForegroundColor Yellow }
    exit 1 }
$script:StageIndex = 0
function Stage([string]$Title) {
    $script:StageIndex++
    Log ""
    Log "=== $($script:StageIndex). $Title ==="
}

# Windows path -> /mnt/<drive>/... (for a given Windows abs path)
function ConvertTo-WslPath([string]$WinPath) {
    $d = $WinPath.Substring(0, 1).ToLower()
    return '/mnt/' + $d + '/' + ($WinPath.Substring(3) -replace '\\', '/').Trim('/')
}

# Write a bash script to a temp .sh (LF, no BOM) and run it in a distro.
# Returns (stdout lines). $LASTEXITCODE is set to the script's exit code.
function Invoke-WslScript {
    param([Parameter(Mandatory)][string]$Distro,
          [Parameter(Mandatory)][string]$ScriptText)
    $id   = [guid]::NewGuid().ToString('N')
    $tmp  = Join-Path $env:TEMP "wsl_$id.sh"
    $out  = Join-Path $env:TEMP "wsl_$id.out"
    $err  = Join-Path $env:TEMP "wsl_$id.err"
    $utf8 = [System.Text.UTF8Encoding]::new($false)
    $norm = ($ScriptText -replace "`r`n", "`n").TrimEnd("`n") + "`n"
    [System.IO.File]::WriteAllText($tmp, $norm, $utf8)

    $wslSh = ConvertTo-WslPath $tmp
    cmd /d /c "`"$env:SystemRoot\System32\wsl.exe`" -d $Distro -- bash `"$wslSh`" > `"$out`" 2> `"$err`""
    $code = $LASTEXITCODE

    $stdout = @()
    if (Test-Path $out) {
        $stdout = [System.IO.File]::ReadAllText($out, $utf8) -split "`r?`n" |
            Where-Object { $_.Trim() }
        Remove-Item $out -Force
    }
    $stderr = @()
    if (Test-Path $err) {
        $stderr = [System.IO.File]::ReadAllText($err, $utf8) -split "`r?`n" |
            Where-Object { $_.Trim() }
        Remove-Item $err -Force
    }
    Remove-Item $tmp -Force
    if (-not $Quiet -and $stderr.Count -gt 0) {
        foreach ($e in $stderr) { Write-Warning $e }
    }
    $script:WslErr = $stderr
    return $stdout
}

function Get-WslDistros {
    $tmp = Join-Path $env:TEMP 'wsl_distros.txt'
    cmd /d /c "`"$env:SystemRoot\System32\wsl.exe`" -l -q > `"$tmp`" 2> nul"
    if (-not (Test-Path $tmp)) { return @() }
    $txt = [System.IO.File]::ReadAllText($tmp, [System.Text.Encoding]::Unicode)
    Remove-Item $tmp -Force
    return @($txt -split "`r?`n" | ForEach-Object { $_.Trim() } | Where-Object { $_ })
}

# ---------------------------------------------------------------- 0. preflight
Stage "Preflight (environment summary)"
Log "OS: $([Environment]::OSVersion.VersionString)"
if (Get-Command py -ErrorAction SilentlyContinue) {
    Log "Python launcher: found ($((py --version 2>&1) -join ' '))  [native daemon, optional]"
} else {
    Log "Python launcher: not found (only needed for the native Qiling daemon, WSL2 path does not need it)"
}
$vendoredBlob = Join-Path $PayloadDir 'blob\libScalable_Encoder.so'
if ($BlobFrom) {
    Log "SSC blob: user-provided (-BlobFrom) - validated later in the deploy stage."
} elseif (Test-Path $vendoredBlob) {
    Log "SSC blob: vendored copy present ($vendoredBlob)."
    Log "  NOTE: this is a proprietary Samsung binary. For legal clarity prefer '-BlobFrom <your own extracted copy>'."
} else {
    Log "WARN: no SSC blob found anywhere (vendored or -BlobFrom)."
    Log "  Streaming with SSC will be silent/empty until you provide one."
}

if (-not (Test-Path $PayloadDir)) {
    Die "payload not found: $PayloadDir" `
        "checkout the submodules: 'git submodule update --init --recursive'"
}
if (-not (Test-Path "$env:SystemRoot\System32\wsl.exe")) {
    Die "WSL is not available. Enable WSL2 first: 'wsl --install' (then reboot)." `
        "run 'wsl --install' in an elevated PowerShell and reboot; if WSL is a legacy install, update via 'wsl --update'"
}

$distros = @(Get-WslDistros)
if ($distros.Count -eq 0) {
    Die "No WSL distro installed. Run 'wsl --install -d Ubuntu' first." `
        "install a distro with 'wsl --install -d Ubuntu' (requires reboot on first install)"
}
if ($Distro) {
    if ($distros -notcontains $Distro) {
        Die "Distro '$Distro' not found. Available: $($distros -join ', ')"
    }
} else {
    $Distro = $distros[0]
    Log "Using first listed distro: $Distro (override with -Distro)"
}

# WSL user home dir (determines where ~/ssc lands)
$wslHome = (Invoke-WslScript $Distro 'echo $HOME; test -z "$HOME" && exit 1' | Select-Object -First 1)
if (-not $wslHome) { Die "Could not resolve WSL home for distro '$Distro'." }
Log "WSL home: $wslHome"
$wslRepo = ConvertTo-WslPath $Repo
Log "Payload source (WSL view): $wslRepo/tools/ssc_payload"

Stage "WSL payload deploy"
# Resolve which blob to deploy: the vendored copy, or the user's own -BlobFrom.
$blobSrc = Join-Path $PayloadDir 'blob'
if ($BlobFrom) {
    if (Test-Path $BlobFrom -PathType Container) {
        $blobFile = Get-ChildItem $BlobFrom -Recurse -Filter 'libScalable_Encoder.so' -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if (-not $blobFile) { Die "No 'libScalable_Encoder.so' found inside: $BlobFrom" }
        $BlobFrom = $blobFile.FullName
    }
    if (-not (Test-Path $BlobFrom -PathType Leaf)) {
        Die "Blob not found: $BlobFrom (give a .so file, or a folder containing libScalable_Encoder.so)"
    }
    $magic = New-Object byte[] 4
    $fs = [System.IO.File]::OpenRead($BlobFrom)
    try { [void]$fs.Read($magic, 0, 4) } finally { $fs.Dispose() }
    if ($magic[0] -ne 0x7F -or $magic[1] -ne 0x45 -or $magic[2] -ne 0x4C -or $magic[3] -ne 0x46) {
        Die "Not an ELF binary: $BlobFrom"
    }
    $blobSrc = $BlobFrom
    Log "Using user-provided blob: $BlobFrom"
} else {
    Log "Using vendored blob: $blobSrc (tip: -BlobFrom <your own extracted copy>)"
}
$blobWsl = ConvertTo-WslPath $blobSrc

Log "==> Deploying SSC payload into ~/ssc ..."
$deployScript = @'
#!/bin/sh
set -e
mkdir -p "$HOME/ssc/blob" "$HOME/ssc/bin" "$HOME/ssc/work" "$HOME/ssc/openssc/build_blob"
SRC="{{WSL_REPO}}/tools/ssc_payload"
BLB="{{BLOB_WSL}}"
if [ -d "$BLB" ]; then cp "$BLB/"* "$HOME/ssc/blob/"; else cp "$BLB" "$HOME/ssc/blob/libScalable_Encoder.so"; fi
cp "$SRC/bin/"* "$HOME/ssc/bin/"
cp "$SRC/build_blob/"* "$HOME/ssc/openssc/build_blob/"
chmod +x "$HOME/ssc/bin/start_sscblobd" "$HOME/ssc/openssc/build_blob/ssc_blob_helper"
echo DEPLOYED
'@ -replace '\{\{WSL_REPO\}\}', $wslRepo -replace '\{\{BLOB_WSL\}\}', $blobWsl

$deployOut = Invoke-WslScript $Distro $deployScript
if (($LASTEXITCODE -ne 0) -or ($deployOut -notmatch 'DEPLOYED')) {
    Die "WSL payload deploy failed:`n$($deployOut -join "`n")`n$($script:WslErr -join "`n")"
}
Log "Payload deployed (blob, daemon source, helper, shims)."

Stage "Daemon path rewrite"
$fixScript = @'
#!/bin/sh
set -e
cd "$HOME/ssc/openssc/build_blob"
grep -q 'home/kaan5' sscblobd.c && sed -i "s|/home/kaan5|$HOME|g" sscblobd.c
sed -i "s|/home/kaan5|$HOME|g" sscenc-blob-run
echo PATHFIXED
'@
$fixOut = Invoke-WslScript $Distro $fixScript
if (($LASTEXITCODE -ne 0) -or ($fixOut -notmatch 'PATHFIXED')) {
    Die "Daemon path rewrite failed:`n$($fixOut -join "`n")`n$($script:WslErr -join "`n")"
}

Stage "Compile sscblobd (gcc -O2)"
if (-not $SkipCompile) {
    $haveGcc = Invoke-WslScript $Distro 'command -v gcc'
    if (-not $haveGcc) {
        if ($SkipApt) {
            Log "WARN: gcc missing and -SkipApt set - build skipped."
        } else {
            Write-Host "gcc not found in WSL. Install build deps [Y/n]?"
            if ((Read-Host).ToLower() -ne 'n') {
                Invoke-WslScript $Distro 'sudo apt-get update -y && sudo apt-get install -y gcc qemu-user gcc-aarch64-linux-gnu' | Out-Host
            }
        }
    }
    $buildOut = Invoke-WslScript $Distro '#!/bin/sh
set -e
cd "$HOME/ssc/openssc/build_blob" && gcc -O2 -o sscblobd sscblobd.c && echo BUILT'
    if (($LASTEXITCODE -ne 0) -or ($buildOut -notmatch 'BUILT')) {
        Die "sscblobd compile failed:`n$($buildOut -join "`n")`n$($script:WslErr -join "`n")"
    }
    Log "sscblobd built."
} else {
    Log "SkipCompile set - not building sscblobd."
}

# ------------------------------------ 5. qemu + aarch64 sysroot
Stage "qemu + aarch64 sysroot"
$qemuOut = Invoke-WslScript $Distro 'command -v qemu-aarch64 && test -d /usr/aarch64-linux-gnu && echo OK'
if (($qemuOut -join "`n") -notmatch 'OK') {
    Log "qemu-user / aarch64 sysroot missing."
    if (-not $SkipApt) {
        Write-Host "Install qemu-user + gcc-aarch64-linux-gnu (needs sudo) [Y/n]?"
        if ((Read-Host).ToLower() -ne 'n') {
            Invoke-WslScript $Distro 'sudo apt-get install -y qemu-user gcc-aarch64-linux-gnu' | Out-Host
        }
    }
} else {
    Log "qemu-aarch64 + sysroot: OK."
}

# ----------------------------------------------------- 6. .wslconfig tuning
Stage ".wslconfig tuning (RAM/idle)"
$canonical = @(
    '',
    '[wsl2]',
    'memory=1GB',
    'vmIdleTimeout=5000',
    'swap=0',
    'sparseVhd=true',
    'nestedVirtualization=false',
    'guiApplications=false',
    '',
    '[experimental]',
    'autoMemoryReclaim=dropcache',
    ''
) -join "`r`n"

if (-not (Test-Path $WslConfigPath)) {
    [System.IO.File]::WriteAllText($WslConfigPath, $canonical, [System.Text.UTF8Encoding]::new($true))
    Log ".wslconfig created (memory=1GB, vmIdleTimeout, swap=0, reclaim=dropcache)."
} elseif ($ForceWslConfig) {
    Copy-Item $WslConfigPath ($WslConfigPath + '.bak') -Force
    [System.IO.File]::WriteAllText($WslConfigPath, $canonical, [System.Text.UTF8Encoding]::new($true))
    Log ".wslconfig overwritten (backup at .wslconfig.bak)."
} else {
    # Merge missing keys into existing sections; never delete user settings.
    $lines = @(Get-Content $WslConfigPath)
    $want = [ordered]@{
        '[wsl2]'        = @('memory=1GB','vmIdleTimeout=5000','swap=0','sparseVhd=true',
                            'nestedVirtualization=false','guiApplications=false')
        '[experimental]'= @('autoMemoryReclaim=dropcache')
    }
    $seen = [ordered]@{}
    foreach ($sec in $want.Keys) { $seen[$sec] = @{} }
    $cur = $null
    foreach ($ln in $lines) {
        $t = $ln.Trim()
        if ($want.Contains($t)) { $cur = $t; continue }
        if ($t -match '^\[')    { $cur = $null; continue }
        if ($cur -and $t -match '^([^=]+)=(.*)$') {
            $match = $Matches[1].Trim() + '=' + $Matches[2].Trim()
            foreach ($kv in $want[$cur]) {
                if ($kv -eq $match) { $seen[$cur][$kv] = $true }
            }
        }
    }
    $result = New-Object System.Collections.ArrayList
    foreach ($l in $lines) { [void]$result.Add($l) }
    $wrote = $false
    foreach ($sec in $want.Keys) {
        if (-not ($lines -contains $sec)) { [void]$result.Add(''); [void]$result.Add($sec) }
        $missing = @($want[$sec] | Where-Object { -not $seen[$sec][$_] })
        if ($missing.Count -gt 0) {
            $idx = $result.IndexOf($sec)
            $at = $idx + 1
            foreach ($m in $missing) { $result.Insert($at, $m); $at++; $wrote = $true }
        }
    }
    if ($wrote) {
        Copy-Item $WslConfigPath ($WslConfigPath + '.bak') -Force
        [System.IO.File]::WriteAllText(
            $WslConfigPath,
            ($result -join "`r`n") + "`r`n",
            [System.Text.UTF8Encoding]::new($true))
        Log ".wslconfig updated (missing Phase-1 keys added; backup at .wslconfig.bak)."
        Log "NOTE: restart WSL to apply: 'wsl --shutdown'"
    } else {
        Log ".wslconfig already has all Phase-1 settings."
    }
}

# ------------------------------------------------------- 7. dongle check
Stage "Dongle driver mode + firmware"
$dev = Get-CimInstance Win32_PnPEntity -Filter "DeviceID LIKE 'USB\\VID_2357&PID_0604%'" -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $dev) {
    Log "No TP-Link UB500 (VID_2357:PID_0604) dongle found."
    Log "  Streaming needs the dongle plugged in. A different dongle may need a chipset/service tweak."
} else {
    Log "Dongle: $($dev.Name)"
    Log "  HW ID: $($dev.PNPDeviceID)"
    if ($dev.Service -eq 'winusb') {
        Log "  Service: winusb  ->  STREAMING MODE (good)."
    } else {
        Log "  Service: '$($dev.Service)' -> Windows BT mode; SSC streaming requires WinUSB."
        Log "  Fix (choose one):"
        Log "    A) Open the GUI -> 'Enable Streaming (WinUSB)' button (auto-elevated, generates a signed INF)."
        Log "    B) Manual: Zadig -> replace driver for USB\VID_2357&PID_0604 with WinUSB."
        Log "  After switching, replug the dongle (or use the in-app toggle which re-evaluates the devnode)."
    }
    if ($dev.PNPDeviceID -match 'VID_2357&PID_0604') {
        Log "  Chipset: Realtek RTL8761B-class - proprietary dongle firmware may be required."
        Log "  Firmware is NOT bundled (proprietary). The app logs when a Realtek firmware file is missing."
    }
}

# ---------------------------------------------------------- 8. smoke (optional)
if ($Smoke) {
    Stage "Smoke test (golden regression 229k)"
    try { $py = (Get-Command python -ErrorAction Stop).Source } catch { Die "-Smoke needs python on PATH." }
    $env:SSC_DAEMON_SCRIPT = "$wslHome/ssc/bin/start_sscblobd"
    $env:SSC_WSL_DISTRO   = $Distro
    & $py (Join-Path $Repo 'tools\golden\ssc_golden.py') check --golden (Join-Path $Repo 'tools\golden\229k.golden')
    if ($LASTEXITCODE -ne 0) { Die "Golden regression FAILED (see above)." }
    Log "Golden 229k: PASS."
}

Log "Done. Setup complete: $Distro @ $wslHome"
Log "Run a stream with:  .\start.ps1 -Codec ssc -Device 78:C1:1D:A7:BC:EE"