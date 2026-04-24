Get-Process | Where-Object { $_.ProcessName -like '*hermes_bridge*' } | Select-Object Id,ProcessName,@{N='CPU(s)';E={$_.CPU}},@{N='Mem(MB)';E={[math]::Round($_.WorkingSet/1MB,1)}},Responding,StartTime | Format-Table -AutoSize
Write-Host "---"
$files = @(
    "C:\lobster\hermes_bridge\cmd_hermes.txt",
    "C:\lobster\hermes_bridge\out_hermes.txt",
    "C:\lobster\hermes_bridge\logs\events.txt"
)
foreach ($f in $files) {
    $info = Get-Item $f -ErrorAction SilentlyContinue
    if ($info) {
        Write-Host "$($f): size=$($info.Length) updated=$($info.LastWriteTime)"
    } else {
        Write-Host "$($f): NOT FOUND"
    }
}
Write-Host "---"
$tail = Get-Content "C:\lobster\hermes_bridge\logs\events.txt" -Tail 20 -ErrorAction SilentlyContinue
$tail
