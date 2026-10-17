# ʕ •ᴥ•ʔ✿ SRP6 registration + logon check (Python when available) ✿ ʕ •ᴥ•ʔ
param(
    [Parameter(Mandatory)][string]$Username,
    [Parameter(Mandatory)][string]$Password,
    [string]$Check
)

$ErrorActionPreference = 'Stop'
$PyScript = Join-Path $PSScriptRoot 'acore-account.py'

function Get-AcorePython {
    $candidates = @(
        "$env:LocalAppData\Programs\Python\Python312\python.exe",
        "$env:LocalAppData\Programs\Python\Python313\python.exe",
        "$env:LocalAppData\Programs\Python\Python311\python.exe"
    )
    foreach ($path in $candidates) {
        if (Test-Path -LiteralPath $path) { return $path }
    }
    $cmd = Get-Command python -ErrorAction SilentlyContinue
    if ($cmd) {
        & $cmd.Source -c "import sys" 2>$null | Out-Null
        if ($LASTEXITCODE -eq 0) { return $cmd.Source }
    }
    return $null
}

$python = Get-AcorePython
if ($python) {
    if ($Check) {
        & $python $PyScript $Username $Password --check $Check
        exit $LASTEXITCODE
    }
    & $python $PyScript $Username $Password
    exit $LASTEXITCODE
}

if ($Check) {
    Write-Error "Python is required for auth logon check. Install Python 3 and re-run."
}

Write-Error @"
Python is required to create AzerothCore SRP6 accounts on Windows.
Install: winget install Python.Python.3.12
Then re-run: $PSScriptRoot\start-docker.ps1
"@
