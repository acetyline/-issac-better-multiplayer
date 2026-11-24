param(
    [string] $GameDirectory,
    [ValidateSet('Prepare', 'Verify', 'Launch')]
    [string] $Action = 'Launch',
    [string] $TransportDll,
    [string] $InstallRoot,
    [string] $OriginalExecutable,
    [int] $SteamEntryProcessId,
    [string] $EncodedGameArguments,
    [switch] $FriendsOnly
)

$ErrorActionPreference = 'Stop'
if ($InstallRoot) { [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false) }
if ($PSVersionTable.PSEdition -eq 'Desktop') {
    foreach ($module in 'Microsoft.PowerShell.Management', 'Microsoft.PowerShell.Utility') {
        Import-Module -Name (Join-Path $PSHOME "Modules/$module/$module.psd1")
    }
}
$package = Split-Path -Parent $PSScriptRoot
$buildPath = if ($FriendsOnly) { 'dist/Friends-Only/Native-P2P' } else { 'dist/Native-P2P' }
$buildPackage = Join-Path $package $buildPath
if (!$GameDirectory) {
    foreach ($location in $package, $buildPackage) {
        $configuration = Join-Path $location 'game-path.txt'
        if (Test-Path -LiteralPath $configuration -PathType Leaf) {
            $GameDirectory = [IO.File]::ReadAllText($configuration).Trim()
            if ($GameDirectory) { break }
        }
    }
    if (!$GameDirectory) {
        $steamPath = (Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
        if (!$steamPath -and ${env:ProgramFiles(x86)}) { $steamPath = Join-Path ${env:ProgramFiles(x86)} 'Steam' }
        if ($steamPath) {
            $libraries = @($steamPath)
            $folderList = Join-Path $steamPath 'steamapps/libraryfolders.vdf'
            if (Test-Path -LiteralPath $folderList -PathType Leaf) {
                foreach ($entry in [regex]::Matches([IO.File]::ReadAllText($folderList), '(?im)^\s*"path"\s*"([^"]+)"')) {
                    $libraries += $entry.Groups[1].Value.Replace('\\', '\')
                }
            }
            foreach ($library in $libraries) {
                $apps = Join-Path $library 'steamapps'
                $candidate = Join-Path $apps 'common/The Binding of Isaac Rebirth'
                if ((Test-Path -LiteralPath (Join-Path $apps 'appmanifest_250900.acf') -PathType Leaf) -and
                    (Test-Path -LiteralPath (Join-Path $candidate 'isaac-ng.exe') -PathType Leaf)) {
                    $GameDirectory = $candidate
                    break
                }
            }
        }
    }
    if (!$GameDirectory) { throw '请用 Install.ps1 指定游戏安装目录。' }
}
if (!$TransportDll) {
    $TransportDll = Join-Path $PSScriptRoot 'bm_steam.dll'
    if (!(Test-Path -LiteralPath $TransportDll -PathType Leaf)) {
        $TransportDll = Join-Path $buildPackage 'transport/bm_steam.dll'
    }
}
$game = [IO.Path]::GetFullPath($GameDirectory)
$stock = if ($OriginalExecutable) { [IO.Path]::GetFullPath($OriginalExecutable) } else { Join-Path $game 'isaac-ng.exe' }
if (!$OriginalExecutable -and $FriendsOnly -and (Test-Path -LiteralPath (Join-Path $game 'BetterMultiplayer-installation.txt') -PathType Leaf)) {
    $entry = try { [Reflection.AssemblyName]::GetAssemblyName($stock).Name } catch [BadImageFormatException] { $null }
    if ($entry -eq 'BetterMultiplayer-0.3.0-preview-J511-Friends-Mods-Setup') {
        $stock = Join-Path $game 'isaac-ng.bettermultiplayer-original.exe'
    }
}
$steam = Join-Path $game 'steam_api.dll'
$ascii = [Text.Encoding]::ASCII
$bytes = [IO.File]::ReadAllBytes($stock)
$steamBytes = [IO.File]::ReadAllBytes($steam)
$stockImports = @(
    'SteamAPI_GetHSteamUser', 'SteamAPI_RegisterCallback', 'SteamAPI_RegisterCallResult',
    'SteamAPI_RunCallbacks', 'SteamAPI_Shutdown', 'SteamAPI_UnregisterCallback', 'SteamAPI_UnregisterCallResult',
    'SteamInternal_ContextInit', 'SteamInternal_FindOrCreateUserInterface', 'SteamInternal_SteamAPI_Init'
)

function Get-Offset {
    param([uint32] $Rva)
    for ($i = 0; $i -lt $sectionCount; $i++) {
        $section = $sections + 40 * $i
        $address = [BitConverter]::ToUInt32($bytes, $section + 12)
        $size = [BitConverter]::ToUInt32($bytes, $section + 16)
        if ($Rva -ge $address -and ($Rva - $address) -lt $size) {
            return [int]([BitConverter]::ToUInt32($bytes, $section + 20) + $Rva - $address)
        }
    }
    throw '请核对 Repentance+ J511 安装文件。'
}

function Get-Name {
    param([int] $Offset)
    $end = [Array]::IndexOf($bytes, [byte]0, $Offset)
    if ($end -lt $Offset -or $end - $Offset -gt 128) { throw '请核对游戏导入表。' }
    return $ascii.GetString($bytes, $Offset, $end - $Offset)
}

function Set-GameInstruction {
    param([uint32] $Rva, [byte[]] $Expected, [byte[]] $Replacement)
    $offset = Get-Offset $Rva
    for ($i = 0; $i -lt $Expected.Length; $i++) {
        if ($bytes[$offset + $i] -ne $Expected[$i]) {
            throw '请通过 Steam 验证 Repentance+ J511 的安装文件。'
        }
    }
    $Replacement.CopyTo($bytes, $offset)
}

function Test-BinaryVersion {
    param([byte[]] $Image, [int] $Length, [int] $Header, [uint32] $Timestamp)
    return $Image.Length -eq $Length -and
        [BitConverter]::ToUInt16($Image, 0) -eq 0x5A4D -and
        [BitConverter]::ToInt32($Image, 0x3c) -eq $Header -and
        [BitConverter]::ToUInt32($Image, $Header) -eq 0x4550 -and
        [BitConverter]::ToUInt16($Image, $Header + 4) -eq 0x14c -and
        [BitConverter]::ToUInt16($Image, $Header + 24) -eq 0x10b -and
        [BitConverter]::ToUInt32($Image, $Header + 8) -eq $Timestamp
}

if (!('BetterMultiplayer.FileBytes' -as [type])) {
    Add-Type -TypeDefinition @'
using System.IO;

namespace BetterMultiplayer
{
    public static class FileBytes
    {
        public static bool Matches(string path, byte[] expected)
        {
            if (!File.Exists(path))
                return false;

            byte[] actual = File.ReadAllBytes(path);
            if (actual.Length != expected.Length)
                return false;

            for (int offset = 0; offset < expected.Length; offset++)
            {
                if (actual[offset] != expected[offset])
                    return false;
            }
            return true;
        }
    }
}
'@
}

function Get-Options {
    $savePath = Join-Path $game 'savedatapath.txt'
    if (Test-Path -LiteralPath $savePath -PathType Leaf) {
        $line = [IO.File]::ReadAllLines($savePath) | Where-Object { $_.StartsWith('Save Data Path:') } | Select-Object -First 1
        $folder = if ($line) { $line.Substring('Save Data Path:'.Length).Trim() }
        if ($folder) {
            $path = Join-Path $folder 'options.ini' -ErrorAction SilentlyContinue
            if ($path -and (Test-Path -LiteralPath $path -PathType Leaf -ErrorAction SilentlyContinue)) {
                return [IO.Path]::GetFullPath($path)
            }
        }
    }
    $documents = [Environment]::GetFolderPath([Environment+SpecialFolder]::MyDocuments)
    if ($documents) {
        $fallback = Join-Path $documents 'My Games/Binding of Isaac Repentance+/options.ini'
        if (Test-Path -LiteralPath $fallback -PathType Leaf) { return $fallback }
    }
    throw '请先通过 Steam 运行一次 Repentance+。'
}

function Update-Crossplay {
    param([string] $Path, [string] $Value)
    $raw = [IO.File]::ReadAllBytes($Path)
    $encoding = if ($raw.Length -ge 2 -and $raw[0] -eq 0xFF -and $raw[1] -eq 0xFE) { [Text.Encoding]::Unicode }
        elseif ($raw.Length -ge 2 -and $raw[0] -eq 0xFE -and $raw[1] -eq 0xFF) { [Text.Encoding]::BigEndianUnicode }
        else { [Text.Encoding]::GetEncoding(28591) }
    $original = $encoding.GetString($raw)
    $pattern = [regex]'(?im)^(?<bom>\uFEFF|\u00EF\u00BB\u00BF)?[ \t]*EosCrossplay[ \t]*(?:=(?<value>[^\r\n]*))?[ \t]*(?:\r?\n|$)'
    $entries = $pattern.Matches($original)
    $previous = '0'
    foreach ($entry in $entries) {
        $candidate = $entry.Groups['value'].Value.Trim()
        if ($candidate -match '^[0-3]$') { $previous = $candidate; break }
    }
    $target = if ($Value) { $Value } else { $previous }
    $lf = [string][char]10
    $crlf = [string][char]13 + $lf
    $lineEnding = if ($original.Contains($crlf)) { $crlf } else { $lf }
    $updated = $original
    if ($entries.Count -eq 0) {
        if ($updated.Length -gt 0 -and !$updated.EndsWith($lf)) { $updated += $lineEnding }
        $updated += "EosCrossplay=$target$lineEnding"
    } else {
        for ($index = $entries.Count - 1; $index -ge 0; $index--) {
            $entry = $entries[$index]
            $line = $entry.Groups['bom'].Value
            if ($index -eq 0) {
                $line += "EosCrossplay=$target"
                if ($entry.Value.EndsWith($lf)) { $line += $lineEnding }
            }
            $updated = $updated.Substring(0, $entry.Index) + $line + $updated.Substring($entry.Index + $entry.Length)
        }
    }
    if ($updated -cne $original) {
        $temp = "$Path.bmtp-$([guid]::NewGuid().ToString('N'))"
        try {
            [IO.File]::WriteAllBytes($temp, $encoding.GetBytes($updated))
            [IO.File]::Replace($temp, $Path, [NullString]::Value)
        }
        finally { if ([IO.File]::Exists($temp)) { [IO.File]::Delete($temp) } }
    }
    return $previous
}

function Invoke-ModSync {
    param([string] $Operation, [string] $Session)
    $arguments = foreach ($value in $Operation, $game, $options, $Session) {
        $trailing = $value.Length - $value.TrimEnd('\').Length
        '"' + $value + ('\' * $trailing) + '"'
    }
    $command = [Diagnostics.ProcessStartInfo]::new($syncHelper, ($arguments -join ' '))
    $command.UseShellExecute = $false
    $command.CreateNoWindow = $true
    $command.RedirectStandardError = $true
    $command.StandardErrorEncoding = [Text.Encoding]::UTF8
    $helper = [Diagnostics.Process]::Start($command)
    try {
        $problem = $helper.StandardError.ReadToEnd().Trim()
        $helper.WaitForExit()
        if ($helper.ExitCode -ne 0) { throw $problem }
    } finally { $helper.Dispose() }
}

function Write-LaunchLog {
    param([string] $Message)
    $timestamp = [DateTimeOffset]::Now.ToString('yyyy-MM-dd HH:mm:ss.fff zzz')
    try {
        [IO.File]::AppendAllText($transportLog, "$timestamp [launcher] $Message`r`n")
    }
    catch [IO.IOException] { Write-Verbose $_.Exception.Message }
    catch [UnauthorizedAccessException] { Write-Verbose $_.Exception.Message }
}

function Wait-ModSyncGame {
    param([Diagnostics.Process] $Process, [string] $Session)
    $scanned = ''
    $apply = Join-Path $Session 'apply.txt'
    $scan = Join-Path $Session 'host-scan.txt'
    $errorFile = Join-Path $Session 'error.txt'
    $nextClose = [DateTime]::MinValue
    $problem = ''
    $route = ''
    while (!$Process.WaitForExit(300)) {
        if (!$problem -and (Test-Path -LiteralPath $errorFile -PathType Leaf)) {
            $issue = [IO.File]::ReadAllText($errorFile).Trim()
            switch ($issue) {
                'setup' { $problem = '请用新版好友联机安装程序重新安装 Mod 同步组件。' }
                'manifest' { $problem = '请房主检查已启用的 Mod 并重新建房。' }
                'timeout' { $problem = 'Mod 同步已超时。请在 Steam 的下载页面检查工坊项目后重新加入。' }
            }
        }
        if (!$problem -and !$route -and (Test-Path -LiteralPath $scan -PathType Leaf)) {
            $request = [IO.File]::ReadAllText($scan).Trim()
            if ($request -match '^[0-9]{1,20}$' -and $request -ne $scanned) {
                try {
                    Invoke-ModSync -Operation scan -Session $Session
                    [IO.File]::WriteAllText((Join-Path $Session 'host-ready.txt'), $request)
                    $scanned = $request
                } catch { $problem = $_.Exception.Message }
            }
        }
        if (!$problem -and !$route -and (Test-Path -LiteralPath $apply -PathType Leaf)) {
            $request = [IO.File]::ReadAllText($apply).Trim()
            if ($request -match '^[0-9]{1,20} [0-9]{1,20}$') { $route = $request }
        }
        if (($route -or $problem) -and [DateTime]::UtcNow -ge $nextClose) {
            if ($nextClose -eq [DateTime]::MinValue) {
                $reason = if ($problem) { 'Mod sync reported an error.' } else { 'Mod sync requested a restart.' }
                Write-LaunchLog "Closing game process $($Process.Id). $reason"
            }
            $Process.Refresh()
            [void]$Process.CloseMainWindow()
            $nextClose = [DateTime]::UtcNow.AddSeconds(3)
        }
    }
    Write-LaunchLog "Game process $($Process.Id) exited with code $($Process.ExitCode)."
    if ($problem) { throw $problem }
    if (!$route -and (Test-Path -LiteralPath $apply -PathType Leaf)) {
        $request = [IO.File]::ReadAllText($apply).Trim()
        if ($request -match '^[0-9]{1,20} [0-9]{1,20}$') { $route = $request }
    }
    return $route
}

if (!(Test-BinaryVersion $bytes 9537032 0x140 0x6B01D5E1) -or
    !(Test-BinaryVersion $steamBytes 266336 0x108 0x672A8EF8)) {
    throw '当前启动配置适用于 Windows Steam 版 Repentance+ 1.9.7.19.J511。'
}
if (!(Test-Path -LiteralPath $TransportDll -PathType Leaf)) { throw '请使用含 bm_steam.dll 的安装包。' }

$header = [BitConverter]::ToInt32($bytes, 0x3c)
$optional = $header + 24
$sectionCount = [BitConverter]::ToUInt16($bytes, $header + 6)
$sections = $optional + [BitConverter]::ToUInt16($bytes, $header + 20)
if ([BitConverter]::ToUInt32($bytes, $header) -ne 0x4550 -or
    [BitConverter]::ToUInt16($bytes, $header + 4) -ne 0x14c -or
    [BitConverter]::ToUInt16($bytes, $optional) -ne 0x10b) { throw '请使用 J511 的 32 位 Steam 游戏文件。' }
$imports = Get-Offset ([BitConverter]::ToUInt32($bytes, $optional + 104))
$steamOffset = -1
for ($index = 0; $index -lt 64; $index++) {
    $descriptor = $imports + $index * 20
    $nameRva = [BitConverter]::ToUInt32($bytes, $descriptor + 12)
    if (!$nameRva) { break }
    $namePosition = Get-Offset $nameRva
    if ((Get-Name $namePosition) -ine 'steam_api.dll') { continue }
    if ($steamOffset -ge 0) { throw '请核对 Steam 入口。' }
    $steamOffset = $namePosition
    $lookup = [BitConverter]::ToUInt32($bytes, $descriptor)
    if (!$lookup) { $lookup = [BitConverter]::ToUInt32($bytes, $descriptor + 16) }
    $entries = Get-Offset $lookup
    $observed = @(foreach ($entry in 0..63) {
        $rva = [BitConverter]::ToUInt32($bytes, $entries + $entry * 4)
        if (!$rva) { break }
        if ($rva -band 0x80000000) { throw '请核对 J511 的 Steam 函数表。' }
        Get-Name ((Get-Offset $rva) + 2)
    })
    if ($observed.Count -ne $stockImports.Count -or (Compare-Object $stockImports $observed)) {
        throw '请核对 J511 的 Steam 函数表。'
    }
}
if ($steamOffset -lt 0) { throw '请核对 J511 的 Steam 入口。' }
$replacement = $ascii.GetBytes('bm_steam.dll')
$replacement.CopyTo($bytes, $steamOffset)
$bytes[$steamOffset + $replacement.Length] = 0

if ($FriendsOnly) {
    # J511 的本地 Mod 提示与重启提示转到联机准备检查的正常返回。
    $modsPrompt = Get-Offset 0x521108
    $restartPrompt = Get-Offset 0x521112
    if ([BitConverter]::ToUInt16($bytes, $modsPrompt) -ne 0x2A74 -or
        [BitConverter]::ToUInt16($bytes, $restartPrompt) -ne 0x1274 -or
        [BitConverter]::ToUInt32($bytes, (Get-Offset 0x50961F)) -ne 0x020A5CE8 -or
        [BitConverter]::ToUInt32($bytes, (Get-Offset 0x5098DA)) -ne 0x00CCD1E8 -or
        [BitConverter]::ToUInt32($bytes, (Get-Offset 0x509C7A)) -ne 0x00C931E8) {
        throw '请通过 Steam 验证 Repentance+ J511 的安装文件。'
    }
    $bytes[$modsPrompt + 1] = 0x0a
    $bytes[$restartPrompt + 1] = 0x00

    $previousFriendsExecutable = [byte[]]$bytes.Clone()
    # 设置读取和游戏菜单统一使用 Steam 模式。
    Set-GameInstruction 0x53ABC8 @(0x8B,0x75,0x08,0x89,0xB1,0xC0,0x00,0x00,0x00) @(0x6A,0x01,0x5E)
    # 跨平台确认窗口直接返回，联机服务选择固定走 Steam。
    Set-GameInstruction 0x59A2FE @(0x0F,0x85,0x8F,0x00,0x00,0x00) @(0xE9,0xD8,0x00,0x00,0x00,0x90)
    Set-GameInstruction 0x643014 @(0x74,0x45,0x80,0x0D) @(0xEB,0x45)
    # EOS 富状态邀请入口直接返回。
    Set-GameInstruction 0x66FE50 @(0x55,0x8B,0xEC,0x6A,0xFF,0x68) @(0xC2,0x04,0x00)
}

$programFiles = [Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)
$installName = if ($FriendsOnly) { 'J511-Friends' } else { 'J511' }
$defaultRoot = Join-Path $programFiles 'BetterMultiplayer/Native-P2P'
$installRoot = if ($InstallRoot) { [IO.Path]::GetFullPath($InstallRoot) } else { $defaultRoot }
$install = Join-Path $installRoot $installName
$ordinaryProcess = 'isaac-ng.bettermultiplayer-p2p'
$friendsProcess = 'isaac-ng.bettermultiplayer-p2p-mods'
$processName = if ($FriendsOnly) { $friendsProcess } else { $ordinaryProcess }
$exe = Join-Path $install "$processName.exe"
$bridge = Join-Path $install 'bm_steam.dll'
$stateDirectory = Join-Path $defaultRoot 'J511'
$state = Join-Path $stateDirectory 'steam-mode.txt'
$transportLog = Join-Path $install 'transport.log'
$sameExecutable = [BetterMultiplayer.FileBytes]::Matches($exe, $bytes)
$requiresPreparation = !(Test-Path -LiteralPath $exe -PathType Leaf) -or !(Test-Path -LiteralPath $bridge -PathType Leaf)
$transportBytes = [IO.File]::ReadAllBytes($TransportDll)
$sameTransport = [BetterMultiplayer.FileBytes]::Matches($bridge, $transportBytes)
if ($FriendsOnly -and (!$sameTransport -or !$sameExecutable)) { $requiresPreparation = $true }
if ($Action -eq 'Prepare' -or ($Action -eq 'Launch' -and $requiresPreparation)) {
    if (Get-Process -Name $ordinaryProcess, $friendsProcess -ErrorAction SilentlyContinue) { throw '请退出联机副本后再更新。' }
    [void][IO.Directory]::CreateDirectory($install)
    if (Test-Path -LiteralPath $exe -PathType Leaf) {
        if (!$sameExecutable) {
            if (!$FriendsOnly -or ![BetterMultiplayer.FileBytes]::Matches($exe, $previousFriendsExecutable)) {
                throw '请先把早期联机副本移入备份目录。'
            }
            $temporaryExe = "$exe.$([guid]::NewGuid().ToString('N'))"
            try {
                [IO.File]::WriteAllBytes($temporaryExe, $bytes)
                [IO.File]::Replace($temporaryExe, $exe, [NullString]::Value)
            }
            finally { [IO.File]::Delete($temporaryExe) }
        }
    } else {
        [IO.File]::WriteAllBytes($exe, $bytes)
    }
    $sameExecutable = $true
    foreach ($dll in Get-ChildItem -LiteralPath $game -Filter '*.dll' -File) {
        if ($dll.Name -ine 'bm_steam.dll') { Copy-Item -LiteralPath $dll.FullName -Destination (Join-Path $install $dll.Name) -Force }
    }
    foreach ($name in 'resources', 'mods', 'data', 'tools') {
        $source = Join-Path $game $name
        $link = Join-Path $install $name
        if (!(Test-Path -LiteralPath $source -PathType Container)) { continue }
        if (Test-Path -LiteralPath $link) {
            $existing = Get-Item -LiteralPath $link -Force
            if ($existing.LinkType -ne 'Junction' -or $source -notin @($existing.Target)) {
                throw "请检查联机副本的 $name 路径。"
            }
        } else {
            New-Item -ItemType Junction -Path $link -Target $source | Out-Null
        }
    }
    Copy-Item -LiteralPath $TransportDll -Destination $bridge -Force
    $sameTransport = [BetterMultiplayer.FileBytes]::Matches($bridge, $transportBytes)
    if ($Action -eq 'Prepare') {
        Write-Output $exe
        return
    }
}

if (!(Test-Path -LiteralPath $exe -PathType Leaf) -or !(Test-Path -LiteralPath $bridge -PathType Leaf)) {
    throw '请先用 Prepare 建立联机副本。'
}
if (!$sameExecutable -or
    !$sameTransport -or
    ![BetterMultiplayer.FileBytes]::Matches((Join-Path $install 'steam_api.dll'), $steamBytes)) {
    throw '请重新核对联机副本并运行 Prepare。'
}
if ($Action -eq 'Verify') {
    Write-Output "已验证 J511 独立 Steam P2P 副本 $exe"
    return
}

$running = Get-Process -Name 'isaac-ng', $ordinaryProcess, $friendsProcess, 'isaac-ng.bettermultiplayer-1.9.7.19' -ErrorAction SilentlyContinue |
    Where-Object Id -ne $SteamEntryProcessId
if ($running) { throw '请退出已打开的以撒进程。' }
if (!(Get-Process -Name 'steam' -ErrorAction SilentlyContinue)) { throw '请先登录 Steam。' }

$options = Get-Options
$gameArguments = if ($EncodedGameArguments) { [Text.Encoding]::Unicode.GetString([Convert]::FromBase64String($EncodedGameArguments)) } else { '' }
$lock = [Threading.Mutex]::new($false, 'Local\BetterMultiplayer-Native-P2P-J511')
$locked = $false
try {
    try { $locked = $lock.WaitOne(0) }
    catch [Threading.AbandonedMutexException] { $locked = $true }
    if (!$locked) { throw '已有 Steam P2P 启动脚本在运行。' }
    [void][IO.Directory]::CreateDirectory($stateDirectory)
    if (Test-Path -LiteralPath $state -PathType Leaf) {
        $previous = [IO.File]::ReadAllText($state).Trim()
        if ($previous -notmatch '^[0-3]$') { throw '请检查联机启动记录。' }
        [void](Update-Crossplay -Path $options -Value $previous)
        [IO.File]::Delete($state)
    }
    $previous = Update-Crossplay -Path $options
    [IO.File]::WriteAllText($state, $previous)
    try {
        $crossplayMode = if ($FriendsOnly) { '1' } else { '0' }
        $syncHelper = Join-Path $PSScriptRoot 'bm_modsync.exe'
        $syncEnabled = $FriendsOnly -and (Test-Path -LiteralPath $syncHelper -PathType Leaf)
        $rejoin = ''
        $restoredRoom = ''
        do {
            [void](Update-Crossplay -Path $options -Value $crossplayMode)
            $start = [Diagnostics.ProcessStartInfo]::new($exe, $gameArguments)
            $gameArguments = ''
            $start.WorkingDirectory = $game
            $start.UseShellExecute = $false
            $start.EnvironmentVariables['SteamAppId'] = '250900'
            $start.EnvironmentVariables['SteamGameId'] = '250900'
            $start.EnvironmentVariables['BETTERMULTIPLAYER_TRANSPORT_LOG'] = $transportLog
            $start.EnvironmentVariables['BETTERMULTIPLAYER_FRIENDS_ONLY'] = if ($FriendsOnly) { '1' } else { '0' }
            if (!$syncEnabled) {
                $process = [Diagnostics.Process]::Start($start)
                try {
                    Write-LaunchLog "Started game process $($process.Id)."
                    $process.WaitForExit()
                    Write-LaunchLog "Game process $($process.Id) exited with code $($process.ExitCode)."
                } finally { $process.Dispose() }
                break
            }
            $session = Join-Path $install ('mod-sync-' + [guid]::NewGuid().ToString('N'))
            [void][IO.Directory]::CreateDirectory($session)
            try {
                Invoke-ModSync -Operation scan -Session $session
                $start.EnvironmentVariables['BETTERMULTIPLAYER_MODSYNC'] = $session
                $start.EnvironmentVariables['BETTERMULTIPLAYER_MODSYNC_REJOIN'] = $rejoin
                $process = [Diagnostics.Process]::Start($start)
                try {
                    $launch = if ($rejoin) { 'Started game for Mod sync rejoin' } else { 'Started game' }
                    Write-LaunchLog "$launch process $($process.Id)."
                    $rejoin = Wait-ModSyncGame -Process $process -Session $session
                }
                finally { $process.Dispose() }
                if ($rejoin) {
                    if ($restoredRoom -eq $rejoin) { throw '请与房主核对游戏实际加载的 Mod 和配置。' }
                    Invoke-ModSync -Operation apply -Session $session
                    $restoredRoom = $rejoin
                }
            } finally {
                foreach ($file in 'local.txt','installed.txt','host-scan.txt','host-ready.txt','host.txt','items.txt','apply.txt','error.txt','update.tmp') {
                    [IO.File]::Delete((Join-Path $session $file))
                }
                [IO.Directory]::Delete($session)
            }
        } while ($rejoin)
    }
    finally {
        [void](Update-Crossplay -Path $options -Value $previous)
        [IO.File]::Delete($state)
    }
}
finally {
    if ($locked) { $lock.ReleaseMutex() }
    $lock.Dispose()
}
