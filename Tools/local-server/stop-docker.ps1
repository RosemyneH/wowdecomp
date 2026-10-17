# ʕ •ᴥ•ʔ✿ Tear down the local acore-docker compose stack ✿ ʕ •ᴥ•ʔ
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $PSScriptRoot 'docker\cdn-env.ps1')

$docker = Get-WowdecompDockerExe
if (-not $docker) {
    Write-Error 'Docker not found.'
}

$serverDir = if ($env:WOWDECOMP_ACORE_DIR) { $env:WOWDECOMP_ACORE_DIR } else { Join-Path $Root 'test\local-server\acore-docker' }
$composeFile = Join-Path $serverDir 'docker-compose.yml'
if (-not (Test-Path -LiteralPath $composeFile)) {
    Write-Host "No compose project at $serverDir"
    exit 0
}

Initialize-WowdecompCdnCompose -Root $Root
$composeArgs = @('compose', '-f', $composeFile)
if ($WowdecompComposeExtra) {
    $composeArgs += $WowdecompComposeExtra
}
$composeArgs += @('down', '--remove-orphans')

& $docker @composeArgs
exit $LASTEXITCODE
