# ʕ •ᴥ•ʔ✿ Stop wowdecomp play-dir clients so exe/dll copies and relaunches do not fight locks ✿ ʕ •ᴥ•ʔ
param(
    [string]$PlayDir = '',
    [int]$WaitMs = 1500
)

$names = @('WowClient64', 'WowClient64.release')
$stopped = $false
foreach ($name in $names) {
    Get-Process -Name $name -ErrorAction SilentlyContinue | ForEach-Object {
        Write-Host "Stopping $($_.ProcessName) (pid $($_.Id))..."
        Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
        $stopped = $true
    }
}

if (-not $stopped) {
    return
}

if ($WaitMs -gt 0) {
    Start-Sleep -Milliseconds $WaitMs
}

if (-not $PlayDir) {
    return
}

$deadline = [datetime]::UtcNow.AddSeconds(8)
while ([datetime]::UtcNow -lt $deadline) {
    $locked = $false
    foreach ($leaf in @('WowClient64.exe', 'WowClient64.release.exe', 'XvidDecoder.dll')) {
        $path = Join-Path $PlayDir $leaf
        if (-not (Test-Path -LiteralPath $path)) { continue }
        try {
            $fs = [IO.File]::Open($path, 'Open', 'Read', 'None')
            $fs.Close()
        } catch {
            $locked = $true
            break
        }
    }
    if (-not $locked) { return }
    Start-Sleep -Milliseconds 250
}

Write-Warning "Some files under $PlayDir may still be locked by a running client."
