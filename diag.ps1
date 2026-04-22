# Test: simple cmd.exe execution (not powershell)
$json = '{"action":"exec","params":{"command":"dir","shell":"cmd"},"cmd_id":"t1"}'
$utf8 = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText('C:\lobster\hermes_bridge\cmd_main.txt', $json, $utf8)
Write-Host "Wrote test command"
