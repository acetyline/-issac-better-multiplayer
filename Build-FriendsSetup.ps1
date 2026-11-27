param(
    [string] $ZigPath
)

$ErrorActionPreference = 'Stop'
$archive = & (Join-Path $PSScriptRoot 'Build.ps1') -FriendsOnly -ZigPath $ZigPath
$release = Split-Path -Parent $archive
$transport = Join-Path $release 'Native-P2P/transport'
$sources = 'FriendsSetup.cs', 'FriendsGameImage.cs', 'FriendsLauncher.cs' |
    ForEach-Object { Join-Path $PSScriptRoot "installer/$_" }
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework/v4.0.30319/csc.exe'
$name = 'BetterMultiplayer-0.4.0-Friends-Mods-Setup.exe'
$output = Join-Path $release $name
$staging = Join-Path $release ('.setup-' + [guid]::NewGuid().ToString('N'))
$temporary = Join-Path $staging $name

[void][IO.Directory]::CreateDirectory($staging)
try {
    $library = Join-Path $transport 'bm_steam.dll'
    $modSync = Join-Path $transport 'bm_modsync.exe'
    $arguments = @(
        '/nologo', '/target:winexe', '/optimize+', '/codepage:65001'
        '/reference:System.Drawing.dll', '/reference:System.Windows.Forms.dll', '/reference:Microsoft.CSharp.dll'
        "/resource:$library,bm_steam.dll", "/resource:$modSync,bm_modsync.exe", "/out:$temporary"
    )
    & $compiler @arguments @sources
    if ($LASTEXITCODE -ne 0) { throw '好友联机安装程序构建失败。' }
    if ([IO.File]::Exists($output)) { [IO.File]::Replace($temporary, $output, [NullString]::Value) }
    else { [IO.File]::Move($temporary, $output) }
}
finally {
    if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
    [IO.Directory]::Delete($staging)
}
Write-Output $output
