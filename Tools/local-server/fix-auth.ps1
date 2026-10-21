# ʕ •ᴥ•ʔ✿ Reset local Docker auth: realm on 127.0.0.1, account a/a, verify logon ✿ ʕ •ᴥ•ʔ
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $PSScriptRoot 'docker\cdn-env.ps1')

$docker = Get-WowdecompDockerExe
if (-not $docker) { throw 'Docker not found.' }

$serverDir = if ($env:WOWDECOMP_ACORE_DIR) { $env:WOWDECOMP_ACORE_DIR } else { Join-Path $Root 'test\local-server\acore-docker' }
$realmHost = Get-WowdecompRealmHost
$regScript = Join-Path $PSScriptRoot 'acore-account.ps1'

$reg = & $regScript -Username 'a' -Password 'a'
$salt = $reg[0].Trim()
$verifier = $reg[1].Trim()

$sql = @"
INSERT INTO acore_auth.account (username, salt, verifier, expansion)
VALUES ('A', UNHEX('$salt'), UNHEX('$verifier'), 2)
ON DUPLICATE KEY UPDATE salt=VALUES(salt), verifier=VALUES(verifier), expansion=VALUES(expansion);
INSERT INTO acore_auth.account_access (id, gmlevel, RealmID)
SELECT id, 3, -1 FROM acore_auth.account WHERE username='A'
ON DUPLICATE KEY UPDATE gmlevel=VALUES(gmlevel), RealmID=VALUES(RealmID);
UPDATE acore_auth.realmlist SET address='$realmHost', localAddress='$realmHost', port=8085, flag=0 WHERE id=1;
"@

$prev = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& $docker compose -f (Join-Path $serverDir 'docker-compose.yml') exec -T ac-database mysql -uroot -ppassword -e $sql *>$null
$ErrorActionPreference = $prev
& $regScript -Username 'a' -Password 'a' -Check '127.0.0.1:3724'
if ($LASTEXITCODE -ne 0) { throw 'Auth logon check failed.' }

$playDir = if ($env:WOWDECOMP_PLAY_DIR) { $env:WOWDECOMP_PLAY_DIR } else { Join-Path $Root 'test\dev-335' }
$wtf = Join-Path $playDir 'WTF\Config.wtf'
if (Test-Path -LiteralPath $wtf) {
    $text = Get-Content -LiteralPath $wtf -Raw
    if ($text -notmatch 'SET realmList ') {
        $text += "`nSET realmList `"localhost`""
    } else {
        $text = [regex]::Replace($text, 'SET realmList\s+"[^"]*"', 'SET realmList "localhost"')
    }
    $realmName = ''
    $prevEa = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $realmName = (& $docker compose -f (Join-Path $serverDir 'docker-compose.yml') exec -T ac-database mysql -uroot -ppassword -N -e "SELECT name FROM acore_auth.realmlist WHERE id=1 LIMIT 1;" 2>$null | Select-Object -First 1).Trim()
    $ErrorActionPreference = $prevEa
    if ($realmName) {
        if ($text -match 'SET realmName\s+"[^"]*"') {
            $text = [regex]::Replace($text, 'SET realmName\s+"[^"]*"', "SET realmName `"$realmName`"")
        } else {
            $text += "`nSET realmName `"$realmName`""
        }
    }
    Set-Content -LiteralPath $wtf -Value $text.TrimEnd() -NoNewline
}
$rl = Join-Path $playDir 'Data\enUS\realmlist.wtf'
if (Test-Path -LiteralPath (Split-Path $rl -Parent)) {
    Set-Content -LiteralPath $rl -Value 'set realmlist "localhost"' -NoNewline
}

Write-Host @"

Auth fixed: 127.0.0.1:3724  world 127.0.0.1:8085
Login: a / a
Play dir: $playDir
"@
