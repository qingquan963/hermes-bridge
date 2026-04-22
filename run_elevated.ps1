param([string]$exe, [string]$args)
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exe
$psi.Arguments = $args
$psi.Verb = 'RunAs'
$psi.UseShellExecute = $true
[System.Diagnostics.Process]::Start($psi)
