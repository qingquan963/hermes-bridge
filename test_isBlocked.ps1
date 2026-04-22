function isBlockedUrl([string]$url) {
    if ($url.Length -lt 7) { return $true }
    $lower = $url.ToLower()
    if ($lower.IndexOf("http://") -ne 0 -and $lower.IndexOf("https://") -ne 0) {
        return $true
    }
    $host_start = $lower.IndexOf("://")
    if ($host_start -eq -1) { return $true }
    $host_start += 3
    $host_end = $lower.IndexOfAny("/?#", $host_start)
    if ($host_end -eq -1) { $host = $lower.Substring($host_start) } else { $host = $lower.Substring($host_start, $host_end - $host_start) }
    if ($host -eq "localhost" -or $host -eq "localhost.") { return $true }
    if ($host.StartsWith("localhost:")) { return $true }
    $ip = $host
    $colon = $ip.IndexOf(':')
    if ($colon -ne -1) { $ip = $ip.Substring(0, $colon) }
    if ($ip.StartsWith("127.")) { return $true }
    if ($ip.StartsWith("10.")) { return $true }
    if ($ip.StartsWith("172.")) {
        $p1 = $ip.IndexOf('.')
        if ($p1 -ne -1) {
            $p2 = $ip.IndexOf('.', $p1 + 1)
            if ($p2 -ne -1) {
                $octet2 = [int]$ip.Substring($p1 + 1, $p2 - $p1 - 1)
                if ($octet2 -ge 16 -and $octet2 -le 31) { return $true }
            }
        }
    }
    if ($ip.StartsWith("192.168.")) { return $true }
    return $false
}

$url = "file://C:/lobster/hermes_bridge/cb3_result.txt"
$result = isBlockedUrl $url
Write-Host "isBlockedUrl('$url') = $result"
if ($result) { Write-Host "Should be blocked, SSRF warning logged." } else { Write-Host "Should be allowed, will proceed to WinHttpCrackUrl." }