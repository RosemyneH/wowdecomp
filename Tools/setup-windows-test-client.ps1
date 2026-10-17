# ʕ •ᴥ•ʔ✿ Symlinked 3.3.5a play tree for wowdecomp on Windows ✿ ʕ •ᴥ•ʔ
param(
    [string]$GameSrc = $(if ($env:WOWDECOMP_GAME_SRC) { $env:WOWDECOMP_GAME_SRC } else { 'W:\client' }),
    [string]$PlayDir = $(if ($env:WOWDECOMP_PLAY_DIR) { $env:WOWDECOMP_PLAY_DIR } else { '' }),
    [string]$RealmHost = $(if ($env:WOWDECOMP_REALM_HOST) { $env:WOWDECOMP_REALM_HOST } else { 'localhost' }),
    [switch]$FullInstall,
    [switch]$RetailWtf,
    [switch]$RetailRealm
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $PlayDir) {
    $PlayDir = Join-Path $Root 'test\dev-335'
}

function New-WowdecompLink {
    param(
        [Parameter(Mandatory)][string]$Source,
        [Parameter(Mandatory)][string]$Destination
    )
    $Source = (Resolve-Path -LiteralPath $Source).Path
    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Force -Recurse
    }
    $parent = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent | Out-Null
    }
    if (Test-Path -LiteralPath $Source -PathType Leaf) {
        try {
            New-Item -ItemType HardLink -Path $Destination -Target $Source | Out-Null
        } catch {
            Copy-Item -LiteralPath $Source -Destination $Destination
        }
        return
    }
    try {
        New-Item -ItemType SymbolicLink -Path $Destination -Target $Source | Out-Null
        return
    } catch {
        # ʕ •ᴥ•ʔ✿ Junction works for dirs without Developer Mode on many Windows installs ✿ ʕ •ᴥ•ʔ
        cmd /c "mklink /J `"$Destination`" `"$Source`"" | Out-Null
        if ($LASTEXITCODE -eq 0) { return }
        throw "Link failed for $Destination (enable Developer Mode or run elevated)."
    }
}

function Copy-WowClientBinary {
    param(
        [string]$Source,
        [string]$Dest
    )
    if (-not (Test-Path -LiteralPath $Source)) {
        Write-Warning "WowClient not built yet ($Source). Data scope will still be prepared."
        Write-Warning "Build: cmake -S . -B build/x64 -G `"Visual Studio 17 2022`" -A x64"
        Write-Warning "       cmake --build build/x64 --config Release --target WowClient --parallel"
        return
    }
    Copy-Item -LiteralPath $Source -Destination $Dest -Force
}

$dataSrc = Join-Path $GameSrc 'Data'
if (-not (Test-Path -LiteralPath $dataSrc)) {
    Write-Error "No Data folder in $GameSrc"
}

New-Item -ItemType Directory -Path $PlayDir -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $Root 'bin\Win') -Force | Out-Null

$binWin = Join-Path $Root 'bin\Win'
$clientExe = Join-Path $binWin 'WowClient64.exe'
Copy-WowClientBinary -Source $clientExe -Dest (Join-Path $PlayDir 'WowClient64.exe')
$wow64 = Join-Path $binWin 'Wow64.exe'
if (Test-Path -LiteralPath $wow64) {
    Copy-WowClientBinary -Source $wow64 -Dest (Join-Path $PlayDir 'Wow64.exe')
}
Get-ChildItem -LiteralPath $binWin -Filter '*.dll' -ErrorAction SilentlyContinue | ForEach-Object {
    Copy-WowClientBinary -Source $_.FullName -Dest (Join-Path $PlayDir $_.Name)
}

$locale = if ($env:WOWDECOMP_LOCALE) { $env:WOWDECOMP_LOCALE } else { 'enUS' }
$dataDst = Join-Path $PlayDir 'Data'
if (Test-Path -LiteralPath $dataDst) {
    Remove-Item -LiteralPath $dataDst -Force -Recurse
}
New-Item -ItemType Directory -Path $dataDst | Out-Null

$localRealm = -not $RetailRealm
Get-ChildItem -LiteralPath $dataSrc | ForEach-Object {
    $base = $_.Name
    if ($localRealm -and $base -eq $locale -and $_.PSIsContainer) {
        $locDst = Join-Path $dataDst $locale
        New-Item -ItemType Directory -Path $locDst | Out-Null
        Get-ChildItem -LiteralPath $_.FullName | ForEach-Object {
            if ($_.Name -eq 'realmlist.wtf') { return }
            New-WowdecompLink -Source $_.FullName -Destination (Join-Path $locDst $_.Name)
        }
        Set-Content -LiteralPath (Join-Path $locDst 'realmlist.wtf') -Value "set realmlist `"$RealmHost`"" -NoNewline
    } else {
        New-WowdecompLink -Source $_.FullName -Destination (Join-Path $dataDst $base)
    }
}

if ($FullInstall) {
    foreach ($name in @('Interface', 'Cache', 'Logs')) {
        $src = Join-Path $GameSrc $name
        if (Test-Path -LiteralPath $src) {
            New-WowdecompLink -Source $src -Destination (Join-Path $PlayDir $name)
        }
    }
} else {
    foreach ($name in @('Interface', 'Cache', 'Logs')) {
        $path = Join-Path $PlayDir $name
        if (Test-Path -LiteralPath $path -PathType Container) {
            $item = Get-Item -LiteralPath $path
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                Remove-Item -LiteralPath $path -Force -Recurse
            }
        }
    }
    New-Item -ItemType Directory -Path (Join-Path $PlayDir 'Cache\WDB') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $PlayDir 'Logs') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $PlayDir 'Interface\FrameXML') -Force | Out-Null
}

$wtf = Join-Path $PlayDir 'WTF'
if (Test-Path -LiteralPath $wtf) {
    Remove-Item -LiteralPath $wtf -Force -Recurse
}
if ($localRealm) {
    New-Item -ItemType Directory -Path $wtf | Out-Null
    @(
        'SET locale "enUS"'
        "SET realmList `"$RealmHost`""
        'SET hwDetect "0"'
        'SET readTOS "1"'
        'SET readEULA "1"'
        'SET gxApi "D3D9"'
    ) | Set-Content -LiteralPath (Join-Path $wtf 'Config.wtf')
} elseif ($RetailWtf -and (Test-Path -LiteralPath (Join-Path $GameSrc 'WTF'))) {
    New-WowdecompLink -Source (Join-Path $GameSrc 'WTF') -Destination $wtf
} elseif (Test-Path -LiteralPath (Join-Path $GameSrc 'WTF')) {
    New-WowdecompLink -Source (Join-Path $GameSrc 'WTF') -Destination $wtf
} else {
    New-Item -ItemType Directory -Path $wtf | Out-Null
    @('SET locale "enUS"', "SET realmList `"$RealmHost`"") | Set-Content -LiteralPath (Join-Path $wtf 'Config.wtf')
}

$frameXml = Join-Path $PlayDir 'Interface\FrameXML'
New-Item -ItemType Directory -Path $frameXml -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $Root 'WoW\Interface\FrameXML\WowDecompVideoOptions.lua') -Destination $frameXml -Force

$layout = if ($FullInstall) { 'full-install (symlinked Interface/Cache/Logs)' } else { 'vanilla (MPQs + wowdecomp FrameXML patch only)' }
Write-Host @"

Playtest tree: $PlayDir  (gitignored)
Game data:     $GameSrc
Layout:        $layout
Realm:         ${RealmHost}:3724

After rebuilds, re-run this script.

  cd $PlayDir
  .\WowClient64.exe
  Server: $Root\Tools\local-server\start-docker.ps1
"@
