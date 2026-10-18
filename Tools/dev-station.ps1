# ʕ •ᴥ•ʔ✿ Windows play scope + optional AzerothCore Docker launch ✿ ʕ •ᴥ•ʔ
param(
    [ValidateSet('scope', 'server', 'launch', 'release', 'all')]
    [string]$Command = 'all'
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$GameSrc = if ($env:WOWDECOMP_GAME_SRC) { $env:WOWDECOMP_GAME_SRC } else { 'W:\client' }
$PlayDir = if ($env:WOWDECOMP_PLAY_DIR) { $env:WOWDECOMP_PLAY_DIR } else { Join-Path $Root 'test\dev-335' }
$BuildDir = if ($env:WOWDECOMP_BUILD_DIR) { $env:WOWDECOMP_BUILD_DIR } else { Join-Path $Root 'build\x64-nowasm' }
$ReleaseBuildDir = if ($env:WOWDECOMP_RELEASE_BUILD_DIR) { $env:WOWDECOMP_RELEASE_BUILD_DIR } else { Join-Path $Root 'build\x64-release' }

function Get-WowdecompCMake {
    if ($env:WOWDECOMP_CMAKE) { return $env:WOWDECOMP_CMAKE }
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $default = 'C:\Program Files\CMake\bin\cmake.exe'
    if (Test-Path -LiteralPath $default) { return $default }
    throw 'cmake not found. Install Kitware CMake or set $env:WOWDECOMP_CMAKE.'
}

function Show-Usage {
    Write-Host @"
Usage: .\Tools\dev-station.ps1 [scope|server|launch|release|all]

  scope    symlink play tree from `$env:WOWDECOMP_GAME_SRC (default W:\client)
  server   start AzerothCore Docker (needs Docker Desktop)
  launch   start server, then WowClient64.exe from the play dir
  release  MSVC Release (/O2 /Ob2 /DNDEBUG, the -O3 equivalent) as WowClient64.release.exe, gxApi Vulkan
  all      scope + server (does not launch the game)

Env: WOWDECOMP_GAME_SRC  WOWDECOMP_PLAY_DIR  WOWDECOMP_REALM_HOST  WOWDECOMP_BUILD_DIR  WOWDECOMP_RELEASE_BUILD_DIR
"@
}

function Invoke-Scope {
    & (Join-Path $Root 'Tools\setup-windows-test-client.ps1') -GameSrc $GameSrc -PlayDir $PlayDir
}

function Invoke-BuildHint {
    $exe = Join-Path $Root 'bin\Win\WowClient64.exe'
    if (Test-Path -LiteralPath $exe) { return }
    Write-Host 'Building WowClient (Release)...'
    $cmake = Get-WowdecompCMake
    if (-not (Test-Path -LiteralPath $BuildDir)) {
        & $cmake -S $Root -B $BuildDir -G 'Visual Studio 17 2022' -A x64 `
            -DWOWDECOMP_DISABLE_SIGNATURE_CHECKS=ON `
            -DWOWDECOMP_WASM_ADDONS=OFF
    }
    & $cmake --build $BuildDir --config Release --target WowClient --parallel
    & (Join-Path $Root 'Tools\setup-windows-test-client.ps1') -GameSrc $GameSrc -PlayDir $PlayDir
}

function Invoke-Server {
    & (Join-Path $Root 'Tools\local-server\start-docker.ps1')
    & (Join-Path $Root 'Tools\local-server\fix-auth.ps1')
}

function Set-GxApi {
    param([string]$Api)
    $cfg = Join-Path $PlayDir 'WTF\Config.wtf'
    if (-not (Test-Path -LiteralPath $cfg)) { return }
    $text = [IO.File]::ReadAllText($cfg)
    if ($text -match 'SET gxApi\s+"[^"]*"') {
        $text = [regex]::Replace($text, 'SET gxApi\s+"[^"]*"', ('SET gxApi "{0}"' -f $Api))
    } else {
        $text = $text.TrimEnd() + "`r`nSET gxApi `"$Api`"`r`n"
    }
    [IO.File]::WriteAllText($cfg, $text)
}

function Invoke-Release {
    # ʕ •ᴥ•ʔ✿ Separate tree so the /O2 client does not replace the dev exe. MSVC has no -O3; /O2 is the top ✿ ʕ •ᴥ•ʔ
    $cmake = Get-WowdecompCMake
    & $cmake -S $Root -B $ReleaseBuildDir -G 'Visual Studio 17 2022' -A x64 `
        -DWOWDECOMP_DISABLE_SIGNATURE_CHECKS=ON `
        -DWOWDECOMP_WASM_ADDONS=OFF `
        '-DWOWCLIENT_OUTPUT_OVERRIDE=WowClient64.release'
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed: $LASTEXITCODE" }
    & $cmake --build $ReleaseBuildDir --config Release --target WowClient --parallel
    if ($LASTEXITCODE -ne 0) { throw "cmake build failed: $LASTEXITCODE" }

    $built = Join-Path $Root 'bin\Win\WowClient64.release.exe'
    if (-not (Test-Path -LiteralPath $built)) { throw "Missing $built" }
    $dest = Join-Path $PlayDir 'WowClient64.release.exe'
    Copy-Item -LiteralPath $built -Destination $dest -Force
    Get-ChildItem (Join-Path $Root 'bin\Win\*.dll') | ForEach-Object {
        Copy-Item $_.FullName -Destination (Join-Path $PlayDir $_.Name) -Force
    }
    Invoke-Server
    Set-GxApi 'Vulkan'
    Write-Host "Release (/O2 /DNDEBUG, gxApi Vulkan): $dest"
    Write-Host "datadir: $PlayDir"
    & $dest -datadir $PlayDir
}

function Invoke-Launch {
    Invoke-BuildHint
    $client = Join-Path $PlayDir 'WowClient64.exe'
    if (-not (Test-Path -LiteralPath $client)) {
        throw "Missing $client - run: .\Tools\dev-station.ps1 scope"
    }
    Invoke-Server
    Write-Host "Launching: $client"
    Write-Host "datadir: $PlayDir"
    & $client -datadir $PlayDir
}

switch ($Command) {
    'scope' { Invoke-Scope }
    'server' { Invoke-Server }
    'launch' { Invoke-Launch }
    'release' { Invoke-Release }
    'all' {
        Invoke-Scope
        Invoke-Server
    }
    default { Show-Usage; exit 1 }
}
