# Write a minimal exec test - just 'echo' which is a builtin in both cmd and powershell
$json = '{"action":"exec","params":{"command":"echo hello"},"cmd_id":"t2"}'
$utf8 = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText('C:\lobster\hermes_bridge\cmd_main.txt', $json, $utf8)
Write-Host "Wrote test"
