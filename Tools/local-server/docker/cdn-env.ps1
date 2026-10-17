# ʕ •ᴥ•ʔ✿ CDN compose extras for AzerothCore Docker on Windows ✿ ʕ •ᴥ•ʔ

function Get-WowdecompGameDataPath {
    param(
        [string]$Root,
        [string]$GameSrc = $(if ($env:WOWDECOMP_GAME_SRC) { $env:WOWDECOMP_GAME_SRC } else { 'W:\client' })
    )
    Join-Path $GameSrc 'Data'
}

function Get-WowdecompDockerExe {
    $cmd = Get-Command docker -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $candidates = @(
        "${env:ProgramFiles}\Docker\Docker\resources\bin\docker.exe"
    )
    foreach ($path in $candidates) {
        if (Test-Path -LiteralPath $path) { return $path }
    }
    return $null
}

function Initialize-WowdecompCdnCompose {
    param([string]$Root)

    $script:WowdecompComposeExtra = @()
    $data = Get-WowdecompGameDataPath -Root $Root

    if (-not (Test-Path -LiteralPath $data)) {
        Write-Warning "No 3.3.5a Data at $data - CDN stays off. Set `$env:WOWDECOMP_GAME_SRC."
        return
    }

    $resolved = (Resolve-Path -LiteralPath $data).Path
    $env:WOWDECOMP_GAME_DATA = $resolved
    $env:WOWDECOMP_CDN_NGINX_CONF = Join-Path $Root 'Tools\local-server\docker\nginx.conf'
    $env:WOWDECOMP_CDN_SIGNATURE = Join-Path $Root 'Tools\local-server\docker\signaturefile'

    $sig = $env:WOWDECOMP_CDN_SIGNATURE
    $patch3 = Join-Path $data 'patch-3.MPQ'
    if ((-not (Test-Path -LiteralPath $sig)) -or ((Get-Item -LiteralPath $sig).Length -eq 0)) {
        if (Test-Path -LiteralPath $patch3) {
            $py = Get-Command python -ErrorAction SilentlyContinue
            if ($py) {
                try {
                    & $py.Source -c "import sys; sys.exit(0)" 2>$null | Out-Null
                    if ($LASTEXITCODE -eq 0) {
                        & $py.Source -c @"
import mpyq
open(r'$sig','wb').write(mpyq.MPQArchive(r'$patch3').read_file('signaturefile'))
"@ 2>$null | Out-Null
                    }
                } catch {
                    # optional CDN signature extraction
                }
            }
            if (-not (Test-Path -LiteralPath $sig) -or (Get-Item -LiteralPath $sig).Length -eq 0) {
                Write-Warning "CDN signaturefile is empty; pip install mpyq or copy signaturefile from patch-3.MPQ."
            }
        }
    }

    $script:WowdecompComposeExtra = @(
        '-f', (Join-Path $Root 'Tools\local-server\docker\streaming.yml')
    )
}

function Get-WowdecompRealmHost {
    if ($env:WOWDECOMP_REALM_HOST) {
        return $env:WOWDECOMP_REALM_HOST
    }
    return '127.0.0.1'
}

function Get-WowdecompCdnHost {
    try {
        $udp = New-Object System.Net.Sockets.UdpClient
        $udp.Connect('1.1.1.1', 80)
        return $udp.Client.LocalEndPoint.Address.ToString()
    } catch {
        return '127.0.0.1'
    } finally {
        if ($udp) { $udp.Dispose() }
    }
}
