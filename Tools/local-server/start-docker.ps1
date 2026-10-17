# ʕ •ᴥ•ʔ✿ AzerothCore via acore-docker + optional MPQ CDN bind mount ✿ ʕ •ᴥ•ʔ
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $PSScriptRoot 'docker\cdn-env.ps1')

$docker = Get-WowdecompDockerExe
if (-not $docker) {
    Write-Error @"
Docker is not installed or not on PATH.
Install Docker Desktop for Windows, start it, then re-run:
  $PSCommandPath
"@ 
}

$dockerDir = Split-Path $docker -Parent
if ($dockerDir -and ($env:Path -notlike "*$dockerDir*")) {
    $env:Path = "$dockerDir;$env:Path"
}

$serverDir = if ($env:WOWDECOMP_ACORE_DIR) { $env:WOWDECOMP_ACORE_DIR } else { Join-Path $Root 'test\local-server\acore-docker' }
New-Item -ItemType Directory -Path (Split-Path $serverDir -Parent) -Force | Out-Null

if (-not (Test-Path -LiteralPath (Join-Path $serverDir '.git'))) {
    git clone --depth 1 https://github.com/azerothcore/acore-docker.git $serverDir
}

Push-Location $serverDir
try {
    $envFile = Join-Path $serverDir '.env'
    if (-not (Test-Path -LiteralPath $envFile)) {
        $dist = Join-Path $serverDir 'env\dist\.env.docker'
        if (Test-Path -LiteralPath $dist) {
            Copy-Item -LiteralPath $dist -Destination $envFile
        }
        @'

DOCKER_AUTH_EXTERNAL_PORT=3724
DOCKER_WORLD_EXTERNAL_PORT=8085
DOCKER_DB_EXTERNAL_PORT=63306
DOCKER_CDN_EXTERNAL_PORT=1119
'@ | Add-Content -LiteralPath $envFile
    }
    if ((Get-Content -LiteralPath $envFile -Raw) -notmatch 'DOCKER_CDN_EXTERNAL_PORT=') {
        Add-Content -LiteralPath $envFile -Value 'DOCKER_CDN_EXTERNAL_PORT=1119'
    }

    Initialize-WowdecompCdnCompose -Root $Root
    $cdnPort = 1119
    $match = Select-String -Path $envFile -Pattern '^DOCKER_CDN_EXTERNAL_PORT=(.+)$' | Select-Object -Last 1
    if ($match) { $cdnPort = $match.Matches[0].Groups[1].Value.Trim() }
    $env:DOCKER_CDN_EXTERNAL_PORT = $cdnPort

    $composeFileArgs = @('-f', (Join-Path $serverDir 'docker-compose.yml'))
    if ($WowdecompComposeExtra) {
        $composeFileArgs += $WowdecompComposeExtra
    }

    function Invoke-AcoreCompose([string[]]$Extra) {
        & $docker compose @composeFileArgs @Extra
    }

    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $docker image inspect 'acore/ac-wotlk-worldserver:master' *>$null
    $needPull = $LASTEXITCODE -ne 0
    $ErrorActionPreference = $prevEap
    if ($needPull) {
        Write-Host 'Pulling images (first run can take a while)...'
        Invoke-AcoreCompose @('pull')
        if ($LASTEXITCODE -ne 0) { throw 'docker compose pull failed' }
    }

    Invoke-AcoreCompose @('up', '-d')
    if ($LASTEXITCODE -ne 0) { throw 'docker compose up failed' }

    $dbPassword = 'password'
    $pwMatch = Select-String -Path $envFile -Pattern '^DOCKER_DB_ROOT_PASSWORD=(.+)$' | Select-Object -Last 1
    if ($pwMatch) { $dbPassword = $pwMatch.Matches[0].Groups[1].Value.Trim() }

    function Invoke-AcoreMysql([string]$Sql) {
        Invoke-AcoreCompose @(
            'exec', '-T', 'ac-database',
            'mysql', '-uroot', "-p$dbPassword", '--batch', '--skip-column-names', '-e', $Sql
        )
    }

    Write-Host 'Waiting for the auth database...'
    $deadline = (Get-Date).AddMinutes(15)
    $ready = $false
    while ((Get-Date) -lt $deadline) {
        $out = Invoke-AcoreMysql "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='acore_auth' AND table_name='account'"
        if ($out -match '^\s*1\s*$') {
            $ready = $true
            break
        }
        Start-Sleep -Seconds 3
    }
    if (-not $ready) {
        Invoke-AcoreCompose @('logs', '--tail', '40', 'ac-db-import', 'ac-database')
        throw 'AzerothCore database was not ready after 15 minutes.'
    }

    $regScript = Join-Path $PSScriptRoot 'acore-account.ps1'
    $reg = & $regScript -Username 'a' -Password 'a'
    $salt = $reg[0].Trim()
    $verifier = $reg[1].Trim()
    $realmHost = Get-WowdecompRealmHost

    $accountSql = @"
INSERT INTO acore_auth.account (username, salt, verifier, expansion) VALUES ('A', UNHEX('$salt'), UNHEX('$verifier'), 2) ON DUPLICATE KEY UPDATE salt=VALUES(salt), verifier=VALUES(verifier), expansion=2;
INSERT INTO acore_auth.account_access (id, gmlevel, RealmID) SELECT id, 3, -1 FROM acore_auth.account WHERE username='A' ON DUPLICATE KEY UPDATE gmlevel=3;
UPDATE acore_auth.realmlist SET address='$realmHost', localAddress='$realmHost', port=8085, flag=0 WHERE id=1;
"@
    Invoke-AcoreMysql $accountSql | Out-Null

    Write-Host 'Waiting for worldserver...'
    $deadline = (Get-Date).AddMinutes(15)
    $worldReady = $false
    while ((Get-Date) -lt $deadline) {
        $logs = Invoke-AcoreCompose @('logs', '--tail', '200', 'ac-worldserver')
        if ($logs -match 'world initialized') {
            $worldReady = $true
            break
        }
        Start-Sleep -Seconds 3
    }
    if (-not $worldReady) {
        Invoke-AcoreCompose @('logs', '--tail', '40', 'ac-worldserver')
        throw 'worldserver did not finish starting.'
    }

    & $regScript -Username 'a' -Password 'a' -Check '127.0.0.1:3724'
    if ($LASTEXITCODE -ne 0) {
        throw 'Auth logon check failed for a/a. Install Python 3 and re-run this script.'
    }

    $cdnLine = 'CDN off (no Data folder).'
    if ($WowdecompComposeExtra) {
        $cdnHost = Get-WowdecompCdnHost
        if (-not $cdnHost) { $cdnHost = '127.0.0.1' }
        $example = Join-Path $Root 'ThirdParty\streamingclient\WoW.mfil.example'
        $mfil = Join-Path $serverDir 'WoW.mfil'
        (Get-Content -LiteralPath $example -Raw).Replace(
            'source=http://yourcdn.com/',
            "source=http://${cdnHost}:${cdnPort}/"
        ) | Set-Content -LiteralPath $mfil -NoNewline
        $cdnLine = "CDN http://${cdnHost}:${cdnPort}/  (WoW.mfil written to $mfil)"
    }

    Write-Host @"

AzerothCore (Docker, gitignored): $serverDir
Auth 127.0.0.1:3724  World 127.0.0.1:8085
$cdnLine
Account: a / a   gmlevel 3 (all realms)
Stop:  $Root\Tools\local-server\stop-docker.ps1
"@
} finally {
    Pop-Location
}
