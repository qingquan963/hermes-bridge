# Write test cmd file
$utf8 = New-Object System.Text.UTF8Encoding $false
$json = '{"action":"exec","params":{"command":"Write-Output test"},"cmd_id":"test-002"}'
[System.IO.File]::WriteAllText('C:\lobster\hermes_bridge\cmd_main.txt', $json, $utf8)
Write-Host "Wrote cmd_main.txt"
[System.IO.File]::ReadAllBytes('C:\lobster\hermes_bridge\cmd_main.txt') | ForEach-Object { $_.ToString('X2') }
