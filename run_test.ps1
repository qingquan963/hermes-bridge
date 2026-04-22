# Test script
$json = '{"action":"exec","params":{"command":"echo hello"},"cmd_id":"q1"}'
$utf8 = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText('C:\lobster\hermes_bridge\cmd_main.txt', $json, $utf8)
$b = [System.IO.File]::ReadAllBytes('C:\lobster\hermes_bridge\cmd_main.txt')
Write-Host "File size: $($b.Length)"
Write-Host "Hex first 10: $([BitConverter]::ToString($b[0..9]))"
