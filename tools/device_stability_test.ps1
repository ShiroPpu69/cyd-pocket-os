param([string]$Port = 'COM6', [int]$Rounds = 10)
$ErrorActionPreference = 'Stop'
$serial = [System.IO.Ports.SerialPort]::new($Port, 115200)
$serial.ReadTimeout = 100
$serial.WriteTimeout = 1000
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$logPath = Join-Path $PSScriptRoot '..\build\stability-test.log'
$writer = [System.IO.StreamWriter]::new($logPath, $false)
$script:hostTimer = [System.Diagnostics.Stopwatch]::StartNew()
$script:pendingNextAt = -1
$script:serialTail = ''
function Receive-For([int]$Milliseconds) {
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    while ($timer.ElapsedMilliseconds -lt $Milliseconds) {
        if ($serial.BytesToRead -gt 0) {
            $chunk = $serial.ReadExisting()
            $writer.Write($chunk)
            $writer.Flush()
            [Console]::Write($chunk)
            $scan = $script:serialTail + $chunk
            if ($script:pendingNextAt -ge 0 -and $scan -match '\[spotify\] job=7 status=(-?\d+) elapsed=(\d+)') {
                $total = $script:hostTimer.ElapsedMilliseconds - $script:pendingNextAt
                $metric = "`n[host] next-response total-ms=$total status=$($Matches[1]) request-ms=$($Matches[2])`n"
                $writer.Write($metric)
                $writer.Flush()
                [Console]::Write($metric)
                $script:pendingNextAt = -1
            }
            $script:serialTail = $scan.Substring([Math]::Max(0, $scan.Length - 160))
        }
        Start-Sleep -Milliseconds 30
    }
}
function Send-Command([string]$Command, [int]$WaitMs) {
    $marker = "`n[host] command=$Command`n"
    $writer.Write($marker)
    [Console]::Write($marker)
    if ($Command -eq 'next') {
        $script:pendingNextAt = $script:hostTimer.ElapsedMilliseconds
        $script:serialTail = ''
    }
    $serial.Write("$Command`n")
    Receive-For $WaitMs
}
try {
    $serial.Open()
    Receive-For 3000
    Send-Command 'diag' 1000
    Send-Command 'selftest' 2000
    Send-Command 'spotify' 25000
    Send-Command 'next' 7000
    Send-Command 'playlists' 100
    Send-Command 'next' 7000
    Send-Command 'home' 3000
    Send-Command 'wifi-off' 1000
    Send-Command 'spotify' 14000
    Send-Command 'home' 1000
    Send-Command 'diag' 1000
    Send-Command 'wifi-on' 3000
    Send-Command "stress $Rounds" ($Rounds * 27000 + 15000)
    Send-Command 'home' 4000
    Send-Command 'spotify' 15000
    Send-Command 'playlists' 10000
    Send-Command 'diag' 1000
    Send-Command 'home' 3000
    Send-Command 'deadlock' 18000
    Send-Command 'diag' 1000
    Send-Command 'deadlock-next' 500
    Send-Command 'diag' 1000
    Send-Command 'deadlock-next' 500
    Send-Command 'diag' 1000
    Send-Command 'deadlock-prev' 500
    Send-Command 'diag' 1000
    Send-Command 'deadlock-prev' 500
    Send-Command 'diag' 1000
    Send-Command 'home' 3000
    Send-Command 'diag' 1000
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
    $writer.Dispose()
}
Write-Output "Saved log: $logPath"
$record = Get-Content -LiteralPath $logPath -Raw
if ($record -match 'Guru Meditation|panic.ed|heap corruption|stop timeout|lifecycle timeout|=FAIL') {
    throw 'Device regression failed: crash, timeout, or self-test failure; inspect saved log.'
}
if ($Rounds -gt 0 -and -not $record.Contains('[stress] DONE')) {
    throw 'Device regression failed: stress test did not complete.'
}
if ($record -notmatch '\[deadlock\] status=200 matches=10' -or
    $record -notmatch '\[deadlock-diag\].*status=200 count=10 page=2 busy=0' -or
    $record -notmatch '\[deadlock-diag\] start=[1-9]\d* time=\d{2}/\d{2}/\d{2} \d{2}:\d{2} UTC\+8 souls=[\d,]+ source=net_worth') {
    throw 'Deadlock validation failed: expected ten records, three-page navigation, match time and souls for the configured test account.'
}
Write-Output 'Regression checks passed: stress completed, no crash/timeout/self-test failure.'
