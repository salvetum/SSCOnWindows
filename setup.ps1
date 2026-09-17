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
#    .\setup.ps1                        # deploy + compile + verify
#    .\setup.ps1 -Smoke                 # ... then run golden regression
#    .\setup.ps1 -Distro Ubuntu         # target a specific WSL distro
#    .\setup.ps1 -SkipCompile           # deploy/paths only (no gcc)
#    .\setup.ps1 -SkipApt               # never propose apt installs
#    .\setup.ps1 -ForceWslConfig        # overwrite .wslconfig (backup kept)
#    .\setup.ps1 -Quiet                 # minimal output
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
    [string]$Repo = 'C:\Projects\SSCOnWindows'
)

$ErrorActionPreference = 'Stop'
$PayloadDir = Join-Path $Repo 'tools\ssc_payload'
$WslConfigPath = Join-Path $env:USERPROFILE '.wslconfig'

function Log  { if (-not $Quiet) { Write-Host $args } }
function Die  { Write-Error $args[0]; exit 1 }

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

# ---------------------------------------------------------------- 0. checks
if (-not (Test-Path $PayloadDir)) {
    Die "payload not found: $PayloadDir"
}
if (-not (Test-Path "$env:SystemRoot\System32\wsl.exe")) {
    Die "WSL is not available. Enable WSL2 first: 'wsl --install' (then reboot)."
}

$distros = @(Get-WslDistros)
if ($distros.Count -eq 0) {
    Die "No WSL distro installed. Run 'wsl --install -d Ubuntu' first."
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

# ------------------------------------------------------------ 1. WSL deploy
Log "==> Deploying vendored SSC payload into ~/ssc ..."
$deployScript = @'
#!/bin/sh
set -e
mkdir -p "$HOME/ssc/blob" "$HOME/ssc/bin" "$HOME/ssc/work" "$HOME/ssc/openssc/build_blob"
SRC="{{WSL_REPO}}/tools/ssc_payload"
cp "$SRC/blob/"* "$HOME/ssc/blob/"
cp "$SRC/bin/"* "$HOME/ssc/bin/"
cp "$SRC/build_blob/"* "$HOME/ssc/openssc/build_blob/"
chmod +x "$HOME/ssc/bin/start_sscblobd" "$HOME/ssc/openssc/build_blob/ssc_blob_helper"
echo DEPLOYED
'@ -replace '\{\{WSL_REPO\}\}', $wslRepo

$deployOut = Invoke-WslScript $Distro $deployScript
if (($LASTEXITCODE -ne 0) -or ($deployOut -notmatch 'DEPLOYED')) {
    Die "WSL payload deploy failed:`n$($deployOut -join "`n")`n$($script:WslErr -join "`n")"
}
Log "Payload deployed (blob, daemon source, helper, shims)."

# -------------------------------------- 2. rewrite hardcoded /home/kaan5 paths
Log "==> Rewriting daemon path defaults -> $wslHome ..."
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

# ------------------------------------------------------------- 3. compile
if (-not $SkipCompile) {
    Log "==> Compiling sscblobd (gcc -O2) ..."
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

# --------------------------------------------------- 4. qemu + aarch64 sysroot
Log "==> Checking qemu-aarch64 + aarch64 sysroot ..."
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

# ----------------------------------------------------- 5. .wslconfig tuning
Log "==> Ensuring .wslconfig (Phase-1 RAM/idle tuning) ..."
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

# ------------------------------------------------------------- 6. driver mode
Log "==> Dongle driver mode (VID 2357:PID 0604) ..."
$dev = Get-CimInstance Win32_PnPEntity -Filter "DeviceID LIKE 'USB\\VID_2357&PID_0604%'" -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $dev) {
    Log "Dongle not present (or not 2357:0604). Driver check skipped."
} elseif ($dev.Service -eq 'winusb') {
    Log "Dongle: WinUSB -> Streaming mode (SSC/A2DPWB path active)."
} elseif ($dev.Service -match 'bth|bthenum') {
    Log "Dongle: BTHUSB -> Windows BT mode (Streaming requires WinUSB; use Zadig or in-app toggle)."
} else {
    Log "Dongle service='$($dev.Service)' status='$($dev.Status)' (unexpected)."
}

# ---------------------------------------------------------- 7. smoke (optional)
if ($Smoke) {
    Log "==> Smoke: SSC golden regression (229k) ..."
    try { $py = (Get-Command python -ErrorAction Stop).Source } catch { Die "-Smoke needs python on PATH." }
    $env:SSC_DAEMON_SCRIPT = "$wslHome/ssc/bin/start_sscblobd"
    $env:SSC_WSL_DISTRO   = $Distro
    & $py (Join-Path $Repo 'tools\golden\ssc_golden.py') check --golden (Join-Path $Repo 'tools\golden\229k.golden')
    if ($LASTEXITCODE -ne 0) { Die "Golden regression FAILED (see above)." }
    Log "Golden 229k: PASS."
}

Log "Done. Setup complete: $Distro @ $wslHome"
Log "Run a stream with:  .\start.ps1 -Codec ssc -Device 78:C1:1D:A7:BC:EE"