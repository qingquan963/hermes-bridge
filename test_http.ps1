# Test http_get action
$utf8 = New-Object System.Text.UTF8Encoding $false
$json = '{"action":"http_get","params":{"url":"https://httpbin.org/get"},"cmd_id":"hg1"}'
[System.IO.File]::WriteAllText('C:\lobster\hermes_bridge\cmd_main.txt', $json, $utf8)
Write-Host "Wrote http_get test"
