# NSSM Service Registration Script
$ErrorActionPreference = 'Continue'

$nssm = "C:\lobster\hermes_bridge\Release\nssm.exe"
$exe = "C:\lobster\hermes_bridge\Release\hermes_bridge.exe"
$svc = "HermesBridge"

Write-Host "[1] Removing existing service if any..."
& $nssm remove $svc confirm 2>&1 | Out-Null

Write-Host "[2] Installing service..."
& $nssm install $svc $exe ""

Write-Host "[3] Setting AppDirectory..."
& $nssm set $svc AppDirectory "C:\lobster\hermes_bridge\Release"

Write-Host "[4] Setting Description..."
& $nssm set $svc Description "Hermes Bridge - Hermes Agent Windows Resource Bridge"

Write-Host "[5] Setting Auto-Start..."
& $nssm set $svc Start SERVICE_AUTO_START

Write-Host "[6] Setting Restart Delay to 5000ms..."
& $nssm set $svc AppRestartDelay 5000

Write-Host "[7] Starting service..."
Start-Service $svc -ErrorAction SilentlyContinue
& net start $svc 2>&1 | Out-Null

Write-Host "[8] Verifying service status..."
sc query $svc
