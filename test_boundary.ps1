# Test 1: Empty file (should be skipped)
$utf8 = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText('C:\lobster\hermes_bridge\cmd_empty.txt', '', $utf8)
# Test 2: Whitespace only
[System.IO.File]::WriteAllText('C:\lobster\hermes_bridge\cmd_ws.txt', '   ', $utf8)
# Test 3: Truncated JSON
[System.IO.File]::WriteAllText('C:\lobster\hermes_bridge\cmd_trunc.txt', '{"action":"exec","para', $utf8)
# Test 4: Invalid JSON
[System.IO.File]::WriteAllText('C:\lobster\hermes_bridge\cmd_bad.txt', 'not json at all', $utf8)
Write-Host "Wrote boundary test files"
