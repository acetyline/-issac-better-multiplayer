param(
    [Parameter(Mandatory)]
    [string] $GameDirectory,
    [string] $ZigPath,
    [switch] $FriendsOnly
)

$ErrorActionPreference = 'Stop'
$game = [IO.Path]::GetFullPath($GameDirectory)
$build = Join-Path $PSScriptRoot 'Build.ps1'
if (Test-Path -LiteralPath $build -PathType Leaf) {
    $buildPath = if ($FriendsOnly) { 'dist/Friends-Only/Native-P2P' } else { 'dist/Native-P2P' }
    $package = Join-Path $PSScriptRoot $buildPath
    & $build -GameDirectory $game -ZigPath $ZigPath -OutputDirectory $package -FriendsOnly:$FriendsOnly | Out-Null
} else {
    $package = $PSScriptRoot
}
$launcher = Join-Path $package 'transport/Start-NativeP2P.ps1'
$exe = & $launcher -Action Prepare -GameDirectory $game -FriendsOnly:$FriendsOnly
[IO.File]::WriteAllText((Join-Path $package 'game-path.txt'), $game)
Write-Output $exe
