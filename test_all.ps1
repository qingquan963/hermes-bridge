# Test file_read action
$utf8 = New-Object System.Text.UTF8Encoding $false
# Create test file first
[System.IO.File]::WriteAllText('C:\lobster\hermes_bridge\test_read.txt', 'Hello World Test Content', $utf8)
# Now write the cmd file
$json = '{"action":"file_read","params":{"path":"C:\\lobster\\hermes_bridge\\test_read.txt"},"cmd_id":"fr1"}'
[System.IO.File]::WriteAllText('C:\lobster\hermes_bridge\cmd_main.txt', $json, $utf8)
Write-Host "Wrote file_read test"
