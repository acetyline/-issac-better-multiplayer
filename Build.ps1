param(
    [string] $GameDirectory,
    [string] $ZigPath,
    [string] $OutputDirectory,
    [switch] $FriendsOnly
)

$ErrorActionPreference = 'Stop'
if (!$OutputDirectory) {
    $relativePath = if ($FriendsOnly) { 'dist/Friends-Only/Native-P2P' } else { 'dist/Native-P2P' }
    $OutputDirectory = Join-Path $PSScriptRoot $relativePath
}
if (!$ZigPath) {
    $localZig = Join-Path ([IO.Path]::GetTempPath()) 'bettermultiplayer-zig-0.15.2/zig-x86_64-windows-0.15.2/zig.exe'
    $zig = Get-Command zig -ErrorAction SilentlyContinue
    if ($zig) { $ZigPath = $zig.Source }
    elseif (Test-Path -LiteralPath $localZig -PathType Leaf) { $ZigPath = $localZig }
    else { throw '请用 ZigPath 指定 Zig 0.15.2。' }
}
$output = [IO.Path]::GetFullPath($OutputDirectory)
$source = Join-Path $PSScriptRoot 'transport'
$destination = Join-Path $output 'transport'
[void][IO.Directory]::CreateDirectory($destination)
$dll = Join-Path $destination 'bm_steam.dll'
$sources = 'bm_transport.c', 'bm_friends.c', 'bm_characters.c', 'bm_goodtrip.c', 'bm_modsync.c', 'bm_settings.c', 'bm_sync_popup.c', 'bm_recovery.c', 'bm_room_recovery.c', 'bm_speed.c', 'bm_locate.c', 'bm_transport.def' |
    ForEach-Object { Join-Path $source $_ }
& $ZigPath cc -target x86-windows-gnu -std=gnu23 -shared -O2 -s -Wall -Wextra -Werror @sources -o $dll
if ($LASTEXITCODE -ne 0) { throw "传输组件构建失败，返回码 $LASTEXITCODE。" }

$files = 'Start-NativeP2P.cmd', 'Install.ps1', 'README.md', 'transport/Start-NativeP2P.ps1'
if ($FriendsOnly) { $files += 'Start-FriendsMods.cmd' }
foreach ($name in $files) {
    $file = if ($FriendsOnly -and $name -eq 'README.md') { 'transport/README-FriendsMods.md' } else { $name }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination (Join-Path $output $name) -Force
}
$components = @('transport/bm_steam.dll')
if ($FriendsOnly) {
    $compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework/v4.0.30319/csc.exe'
    $modSync = Join-Path $destination 'bm_modsync.exe'
    $compilerOptions = @(
        '/nologo', '/target:exe', '/optimize+', '/codepage:65001'
        '/reference:System.Xml.Linq.dll', '/reference:System.Web.Extensions.dll'
        "/out:$modSync", (Join-Path $source 'ModSync.cs')
    )
    & $compiler @compilerOptions
    if ($LASTEXITCODE -ne 0) { throw 'Mod 同步组件构建失败。' }
    $components += 'transport/bm_modsync.exe'
}
if ($GameDirectory) {
    [IO.File]::WriteAllText((Join-Path $output 'game-path.txt'), [IO.Path]::GetFullPath($GameDirectory))
}

$archiveName = if ($FriendsOnly) {
    'BetterMultiplayer-0.3.0-preview-J511-Friends-Mods.zip'
} else {
    'BetterMultiplayer-0.3.0-preview.zip'
}
$archive = Join-Path (Split-Path -Parent $output) $archiveName
$temporary = "$archive.$([guid]::NewGuid().ToString('N'))"
Add-Type -AssemblyName System.IO.Compression.FileSystem, System.IO.Compression
try {
    $zip = [IO.Compression.ZipFile]::Open($temporary, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($name in $files + $components) {
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, (Join-Path $output $name), $name)
        }
    } finally { $zip.Dispose() }
    if ([IO.File]::Exists($archive)) { [IO.File]::Replace($temporary, $archive, [NullString]::Value) }
    else { [IO.File]::Move($temporary, $archive) }
} finally { if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) } }
Write-Output $archive
