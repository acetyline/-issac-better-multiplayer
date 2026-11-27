using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using Microsoft.Win32.SafeHandles;

internal sealed class FriendsLauncher
{
    private const string SteamLibrary = "steam_api.dll";
    internal const string Bridge = "bm_steam.dll";
    internal const string ModSync = "bm_modsync.exe";
    internal const string ProcessName = "isaac-ng.bettermultiplayer-p2p-mods";
    private static readonly string[] LinkedDirectories = { "resources", "mods", "data", "tools" };
    private static readonly string[] SessionFiles = {
        "local.txt", "installed.txt", "blocked-mods.txt", "scan.txt", "scan-ready.txt",
        "host.txt", "items.txt", "apply.txt", "error.txt", "update.tmp"
    };
    private static readonly Regex ScanRequest = new Regex(@"^[0-9]{1,20} ([01]) [0-9]{1,20}$");
    private static readonly Regex JoinRoute = new Regex(@"^[0-9]{1,20} [0-9]{1,20}$");
    private readonly string game, transport, install, executable, log;
    private readonly FriendsGameImage image;
    private readonly byte[] steam, library, bootstrap;

    internal FriendsLauncher(string game, string original, string nativeRoot, string transport)
    {
        this.game = game;
        this.transport = transport;
        steam = File.ReadAllBytes(Path.Combine(game, SteamLibrary));
        image = new FriendsGameImage(original, steam);
        if (image.NeedsChineseBootstrap)
        {
            string path = Path.Combine(game, "bootstp.dll");
            if (!File.Exists(path))
                throw new FileNotFoundException("游戏程序已打过中文补丁，但缺少 bootstp.dll。请用汉化安装器修复后重试。");
            bootstrap = File.ReadAllBytes(path);
        }
        library = File.ReadAllBytes(Path.Combine(transport, Bridge));
        install = Path.Combine(nativeRoot, image.Build + "-Friends");
        executable = Path.Combine(install, ProcessName + ".exe");
        log = Path.Combine(install, "transport.log");
    }

    private bool IsPrepared()
    {
        return image.Matches(executable) && FriendsGameImage.Matches(Path.Combine(install, Bridge), library) &&
            FriendsGameImage.Matches(Path.Combine(install, SteamLibrary), steam) &&
            (bootstrap == null || FriendsGameImage.Matches(Path.Combine(install, "bootstp.dll"), bootstrap));
    }

    internal void Prepare(bool reinstall)
    {
        if (!reinstall && IsPrepared())
            return;

        Directory.CreateDirectory(install);
        image.Publish(executable);
        foreach (string file in Directory.EnumerateFiles(game, "*.dll"))
        {
            string name = Path.GetFileName(file);
            if (!String.Equals(name, Bridge, StringComparison.OrdinalIgnoreCase))
                File.Copy(file, Path.Combine(install, name), true);
        }
        foreach (string name in LinkedDirectories)
        {
            string source = Path.Combine(game, name);
            if (Directory.Exists(source))
                GameDirectoryLink.Ensure(Path.Combine(install, name), source);
        }
        FriendsInstallation.Publish(Path.Combine(install, Bridge), path => File.WriteAllBytes(path, library));
        if (!IsPrepared())
            throw new IOException("联机副本准备失败，请重新运行安装程序。");
    }

    internal void Launch(string[] arguments)
    {
        string helper = Path.Combine(transport, ModSync);
        if (!File.Exists(helper))
            throw new FileNotFoundException("找不到 Mod 同步组件，请重新安装 Better Multiplayer。");

        string gameArguments = arguments == null ? "" : String.Join(" ", Array.ConvertAll(arguments, FriendsInstallation.Quote));
        string rejoin = "", previousRoute = "", modsOverride = "";
        do
        {
            string session = Path.Combine(install, "mod-sync-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(session);
            try
            {
                RunModSync(helper, "prepare", session);
                var start = new ProcessStartInfo(executable, gameArguments) { WorkingDirectory = game, UseShellExecute = false };
                gameArguments = "";
                start.EnvironmentVariables["SteamAppId"] = "250900";
                start.EnvironmentVariables["SteamGameId"] = "250900";
                start.EnvironmentVariables["BETTERMULTIPLAYER_TRANSPORT_LOG"] = log;
                start.EnvironmentVariables["BETTERMULTIPLAYER_FRIENDS_ONLY"] = "1";
                start.EnvironmentVariables["BETTERMULTIPLAYER_MODSYNC"] = session;
                start.EnvironmentVariables["BETTERMULTIPLAYER_MODSYNC_REJOIN"] = rejoin;
                if (modsOverride.Length != 0)
                    start.EnvironmentVariables["BETTERMULTIPLAYER_MODS_ENABLED"] = modsOverride;
                using (var process = Process.Start(start))
                {
                    Note((rejoin.Length == 0 ? "Started game process " : "Started game for Mod sync rejoin, process ") +
                        process.Id + ".");
                    rejoin = WaitForGame(process, helper, session);
                }
                if (rejoin.Length != 0)
                {
                    if (rejoin == previousRoute)
                        throw new IOException("请与房主核对游戏实际加载的 Mod 和配置。");
                    modsOverride = RunModSync(helper, "apply", session);
                    if (modsOverride != "0" && modsOverride != "1")
                        throw new IOException("Mod 同步组件未返回有效的 Mod 开关。");
                    previousRoute = rejoin;
                }
            }
            finally
            {
                foreach (string file in SessionFiles)
                    File.Delete(Path.Combine(session, file));
                Directory.Delete(session);
            }
        } while (rejoin.Length != 0);
    }

    private string RunModSync(string helper, string operation, string session, string enabled = "")
    {
        string arguments = String.Join(" ", new[] { operation, game, session, enabled }.Select(FriendsInstallation.Quote));
        var start = new ProcessStartInfo(helper, arguments) {
            UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = game,
            RedirectStandardOutput = true, RedirectStandardError = true,
            StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8
        };
        using (var process = Process.Start(start))
        {
            var output = process.StandardOutput.ReadToEndAsync();
            var errors = process.StandardError.ReadToEndAsync();
            process.WaitForExit();
            if (process.ExitCode != 0)
            {
                string detail = errors.Result.Trim();
                throw new IOException(detail.Length != 0 ? detail :
                    "Mod 同步组件提前退出，退出码 " + process.ExitCode + "。");
            }
            return output.Result.Trim();
        }
    }

    private static string ReadSession(string session, string name)
    {
        try { return File.ReadAllText(Path.Combine(session, name)).Trim(); }
        catch (FileNotFoundException) { return ""; }
    }

    private string WaitForGame(Process process, string helper, string session)
    {
        string scanned = "", route = "", problem = "";
        DateTime nextClose = DateTime.MinValue;
        while (!process.WaitForExit(300))
        {
            if (problem.Length == 0)
                problem = SyncError(ReadSession(session, "error.txt"));
            if (problem.Length == 0 && route.Length == 0)
            {
                string request = ReadSession(session, "scan.txt");
                Match match = ScanRequest.Match(request);
                if (match.Success && request != scanned)
                {
                    try
                    {
                        RunModSync(helper, "scan", session, match.Groups[1].Value);
                        File.WriteAllText(Path.Combine(session, "scan-ready.txt"), request);
                        scanned = request;
                    }
                    catch (IOException error) { problem = error.Message; }
                    catch (Win32Exception error) { problem = error.Message; }
                }
                string candidate = ReadSession(session, "apply.txt");
                if (JoinRoute.IsMatch(candidate))
                    route = candidate;
            }
            if ((route.Length != 0 || problem.Length != 0) && DateTime.UtcNow >= nextClose)
            {
                if (nextClose == DateTime.MinValue)
                    Note("Closing game process " + process.Id + " for Mod synchronization.");
                process.Refresh();
                process.CloseMainWindow();
                nextClose = DateTime.UtcNow.AddSeconds(3);
            }
        }
        Note("Game process " + process.Id + " exited with code " + process.ExitCode + ".");
        if (problem.Length == 0)
            problem = SyncError(ReadSession(session, "error.txt"));
        if (problem.Length != 0)
            throw new IOException(problem);
        if (route.Length == 0)
            route = ReadSession(session, "apply.txt");
        return JoinRoute.IsMatch(route) ? route : "";
    }

    private static string SyncError(string issue)
    {
        switch (issue)
        {
            case "setup": return "请用新版好友联机安装程序重新安装 Mod 同步组件。";
            case "goodtrip": return "内置快传没有加载。请重新启动游戏，并检查 transport.log。";
            case "rng": return "共享随机数初始化失败。若已启用 TimeMachine，请检查它的版本和 transport.log。";
            case "manifest": return "请房主检查已启用的 Mod 并重新建房。";
            case "timeout": return "Mod 同步已超时。请在 Steam 的下载页面检查工坊项目后重新加入。";
            default: return "";
        }
    }

    private void Note(string message)
    {
        try { File.AppendAllText(log, DateTimeOffset.Now.ToString("yyyy-MM-dd HH:mm:ss.fff zzz") + " [launcher] " + message + "\r\n"); }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }
}

internal static class GameDirectoryLink
{
    private const uint JunctionTag = 0xa0000003;
    private const uint SetReparsePoint = 0x000900a4, GetReparsePoint = 0x000900a8;
    private const uint OpenJunction = 0x02200000;

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr security,
        uint disposition, uint flags, IntPtr template);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool DeviceIoControl(SafeFileHandle file, uint command, byte[] input, int inputLength,
        byte[] output, int outputLength, out int transferred, IntPtr overlapped);

    internal static void Ensure(string link, string target)
    {
        string destination = target.StartsWith(@"\\", StringComparison.Ordinal)
            ? @"\??\UNC\" + target.Substring(2) : @"\??\" + target;
        if (Directory.Exists(link) || File.Exists(link))
        {
            using (var handle = Open(link, 0))
            {
                var data = new byte[16 * 1024];
                int used;
                if (DeviceIoControl(handle, GetReparsePoint, null, 0, data, data.Length, out used, IntPtr.Zero) &&
                    used >= 16 && BitConverter.ToUInt32(data, 0) == JunctionTag)
                {
                    int offset = BitConverter.ToUInt16(data, 8) + 16;
                    int length = BitConverter.ToUInt16(data, 10);
                    if (offset + length <= used &&
                        String.Equals(Encoding.Unicode.GetString(data, offset, length), destination,
                                      StringComparison.OrdinalIgnoreCase))
                        return;
                }
                throw new IOException("联机副本中的目录链接指向了其他位置，请检查 " + link + "。");
            }
        }

        Directory.CreateDirectory(link);
        try
        {
            using (var handle = Open(link, 0x40000000))
            {
                byte[] substitute = Encoding.Unicode.GetBytes(destination);
                byte[] printable = Encoding.Unicode.GetBytes(target);
                var data = new byte[16 + substitute.Length + 2 + printable.Length + 2];
                BitConverter.GetBytes(JunctionTag).CopyTo(data, 0);
                BitConverter.GetBytes(checked((ushort)(data.Length - 8))).CopyTo(data, 4);
                BitConverter.GetBytes(checked((ushort)substitute.Length)).CopyTo(data, 10);
                BitConverter.GetBytes(checked((ushort)(substitute.Length + 2))).CopyTo(data, 12);
                BitConverter.GetBytes(checked((ushort)printable.Length)).CopyTo(data, 14);
                substitute.CopyTo(data, 16);
                printable.CopyTo(data, 16 + substitute.Length + 2);
                int used;
                if (!DeviceIoControl(handle, SetReparsePoint, data, data.Length, null, 0, out used, IntPtr.Zero))
                    throw LinkError(link, Marshal.GetLastWin32Error());
            }
        }
        catch
        {
            Directory.Delete(link);
            throw;
        }
    }

    private static SafeFileHandle Open(string path, uint access)
    {
        SafeFileHandle handle = CreateFile(path, access, 7, IntPtr.Zero, 3, OpenJunction, IntPtr.Zero);
        if (!handle.IsInvalid)
            return handle;
        int error = Marshal.GetLastWin32Error();
        handle.Dispose();
        throw LinkError(path, error);
    }

    private static IOException LinkError(string path, int error)
    {
        return new IOException("无法设置联机目录 " + path + "。" + new Win32Exception(error).Message);
    }
}
