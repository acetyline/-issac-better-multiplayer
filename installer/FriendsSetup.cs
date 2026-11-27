using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using Microsoft.Win32;

[assembly: AssemblyTitle("Better Multiplayer 好友联机安装")]
[assembly: AssemblyProduct("Better Multiplayer")]
[assembly: AssemblyVersion(Program.Version + ".0")]
[assembly: AssemblyFileVersion(Program.Version + ".0")]
[assembly: AssemblyInformationalVersion(Program.Version)]

internal static class Program
{
    internal const string Title = "Better Multiplayer";
    internal const string Version = "0.4.0";

    [STAThread]
    private static void Main(string[] arguments)
    {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);

        string entry = Path.GetFileName(Application.ExecutablePath);
        string location = Path.GetDirectoryName(Application.ExecutablePath);
        if (string.Equals(entry, FriendsInstallation.SteamExecutableName, StringComparison.OrdinalIgnoreCase))
        {
            try
            {
                using (var process = Process.GetCurrentProcess())
                    FriendsInstallation.ReadSteam(location, FriendsInstallation.Home).Launch(process.Id, arguments);
            }
            catch (Exception error)
            {
                MessageBox.Show(error.Message, Title, MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            return;
        }

        if (string.Equals(entry, FriendsInstallation.UninstallerName, StringComparison.OrdinalIgnoreCase))
        {
            if (MessageBox.Show("卸载 Better Multiplayer 并恢复 Steam 原版启动？", Title,
                MessageBoxButtons.YesNo, MessageBoxIcon.Question, MessageBoxDefaultButton.Button2) == DialogResult.Yes)
            {
                try { FriendsInstallation.HandOffUninstall(location); }
                catch (Exception error)
                {
                    MessageBox.Show(error.Message, Title, MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
            }
            return;
        }

        int setupId;
        if ((arguments.Length == 2 || arguments.Length == 3) && arguments[0] == "/uninstall" &&
            int.TryParse(arguments[1], out setupId))
        {
            try
            {
                FriendsInstallation.WaitForExit(setupId);
                FriendsInstallation.Uninstall(FriendsInstallation.Home,
                    Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
                    arguments.Length == 3 ? arguments[2] : null);
                MessageBox.Show("好友联机已卸载。", Title, MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
            catch (Exception error)
            {
                MessageBox.Show(error.Message, Title, MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            return;
        }

        if (arguments.Length == 1 && arguments[0] == "/play")
        {
            try
            {
                FriendsInstallation.Read(FriendsInstallation.Home).Launch();
            }
            catch (Exception error)
            {
                MessageBox.Show(error.Message, Title, MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            return;
        }

        Application.Run(new SetupWindow());
    }
}

internal sealed class SetupWindow : Form
{
    private readonly TextBox directory = new TextBox();
    private readonly Button browse = new Button { Text = "浏览…", Width = 84 };
    private readonly Button install = new Button { Text = "安装", Width = 104, Height = 34 };
    private readonly Button play = new Button { Text = "启动游戏", Width = 104, Height = 34 };
    private readonly Button uninstall = new Button { Text = "卸载", Width = 104, Height = 34 };
    private readonly Label status = new Label { AutoSize = true, MaximumSize = new Size(520, 0) };
    private bool busy;
    private FriendsInstallation current;

    internal SetupWindow()
    {
        Text = Program.Title + " " + Program.Version;
        Font = new Font("Segoe UI", 10);
        AutoScaleMode = AutoScaleMode.Dpi;
        StartPosition = FormStartPosition.CenterScreen;
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        MinimizeBox = false;
        ClientSize = new Size(576, 238);
        BackColor = Color.White;
        Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);

        var layout = new TableLayoutPanel
        {
            Dock = DockStyle.Fill, Padding = new Padding(24, 18, 24, 14),
            ColumnCount = 2, RowCount = 6
        };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 30));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 25));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 35));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        Controls.Add(layout);

        var heading = new Label
        {
            Text = Program.Title, AutoSize = true, Margin = new Padding(0),
            Font = new Font(Font.FontFamily, 24, FontStyle.Bold), ForeColor = Color.FromArgb(27, 36, 54)
        };
        var subtitle = new Label
        {
            Text = "《以撒的结合：忏悔+》J511 / J514 好友联机", AutoSize = true, Margin = new Padding(0),
            ForeColor = Color.FromArgb(71, 84, 103)
        };
        var directoryLabel = new Label { Text = "游戏目录", AutoSize = true, Margin = new Padding(0) };
        layout.Controls.Add(heading, 0, 0);
        layout.Controls.Add(subtitle, 0, 1);
        layout.Controls.Add(directoryLabel, 0, 2);
        layout.SetColumnSpan(heading, 2);
        layout.SetColumnSpan(subtitle, 2);
        directory.AccessibleName = "游戏目录";
        directory.Dock = DockStyle.Fill;
        directory.Margin = new Padding(0, 2, 8, 0);
        browse.Margin = new Padding(0);
        layout.Controls.Add(directory, 0, 3);
        layout.Controls.Add(browse, 1, 3);

        status.Margin = new Padding(0, 3, 0, 0);
        layout.Controls.Add(status, 0, 4);
        layout.SetColumnSpan(status, 2);

        var actions = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill, AutoSize = true, FlowDirection = FlowDirection.RightToLeft,
            WrapContents = false, Margin = new Padding(0)
        };
        actions.Controls.Add(install);
        actions.Controls.Add(play);
        actions.Controls.Add(uninstall);
        layout.Controls.Add(actions, 0, 5);
        layout.SetColumnSpan(actions, 2);
        install.FlatStyle = FlatStyle.Flat;
        install.FlatAppearance.BorderSize = 0;
        install.BackColor = Color.FromArgb(32, 79, 169);
        install.ForeColor = Color.White;
        AcceptButton = install;

        browse.Click += Browse;
        install.Click += Install;
        play.Click += Play;
        uninstall.Click += Uninstall;
        directory.TextChanged += delegate { RefreshActions(); };
        FormClosing += delegate(object sender, FormClosingEventArgs eventArgs) { eventArgs.Cancel = busy; };

        current = FriendsInstallation.TryRead(FriendsInstallation.Home);
        directory.Text = current == null ? SteamGame.Find() : current.GameDirectory;
        SetStatus(current != null ? "已安装，可直接启动游戏。" :
            directory.TextLength > 0 ? "已找到 Steam 游戏目录。" : "请选择包含 isaac-ng.exe 的游戏目录。", false);
        RefreshActions();
    }

    private void RefreshActions()
    {
        bool currentDirectory = current != null && current.Matches(directory.Text);
        directory.Enabled = !busy;
        browse.Enabled = !busy;
        install.Enabled = !busy && directory.TextLength > 0;
        install.Text = currentDirectory ? "重新安装" : "安装";
        play.Visible = currentDirectory;
        play.Enabled = !busy;
        uninstall.Enabled = !busy && (current != null || FriendsInstallation.HasSteamInstallation(directory.Text));
    }

    private void SetStatus(string message, bool error)
    {
        status.Text = message;
        status.ForeColor = error ? Color.FromArgb(172, 39, 41) : Color.FromArgb(71, 84, 103);
    }

    private void Browse(object sender, EventArgs eventArgs)
    {
        using (var picker = new FolderBrowserDialog
        {
            Description = "选择《以撒的结合：忏悔+》游戏目录",
            SelectedPath = Directory.Exists(directory.Text) ? directory.Text : "",
            ShowNewFolderButton = false
        })
        {
            if (picker.ShowDialog(this) == DialogResult.OK)
                directory.Text = picker.SelectedPath;
        }
    }

    private async void Install(object sender, EventArgs eventArgs)
    {
        string selected = directory.Text.Trim();
        busy = true;
        RefreshActions();
        SetStatus("正在准备好友联机版…", false);
        try
        {
            string desktop = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
            current = await Task.Run(() => FriendsInstallation.Install(selected, FriendsInstallation.Home, desktop));
            directory.Text = current.GameDirectory;
            SetStatus("安装完成。可从 Steam 或桌面快捷方式启动。", false);
        }
        catch (UnauthorizedAccessException error)
        {
            SetStatus("权限不足，需要管理员权限或解除拦截。", true);
            string prompt = "对路径的访问被拒绝，通常是因为：\n" +
                "1. 游戏安装在 C:\\Program Files 等受保护目录，需要管理员权限；\n" +
                "2. 直接在压缩包内双击运行，未先将 installer.zip 解压出来；\n" +
                "3. Windows 安全中心或杀毒软件拦截了文件写入。\n\n" +
                "是否立即尝试以管理员身份重新打开安装程序？\n\n原始错误：" + error.Message;
            if (MessageBox.Show(this, prompt, Program.Title, MessageBoxButtons.YesNo, MessageBoxIcon.Warning) == DialogResult.Yes)
            {
                try
                {
                    Process.Start(new ProcessStartInfo(Application.ExecutablePath) { UseShellExecute = true, Verb = "runas" });
                    Close();
                }
                catch { }
            }
        }
        catch (Exception error)
        {
            SetStatus("安装提示，请查看弹窗。", true);
            MessageBox.Show(this, error.Message, Program.Title, MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
        finally
        {
            busy = false;
            RefreshActions();
        }
    }

    private void Play(object sender, EventArgs eventArgs)
    {
        try
        {
            Process.Start(current.Executable, "/play").Dispose();
            Close();
        }
        catch (Exception error)
        {
            MessageBox.Show(this, error.Message, Program.Title, MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private async void Uninstall(object sender, EventArgs eventArgs)
    {
        if (MessageBox.Show(this, "卸载 Better Multiplayer 并恢复 Steam 原版启动？",
            Program.Title, MessageBoxButtons.YesNo, MessageBoxIcon.Question,
            MessageBoxDefaultButton.Button2) != DialogResult.Yes)
            return;

        try
        {
            string selected = FriendsInstallation.HasSteamInstallation(directory.Text) ? directory.Text.Trim() :
                (current == null ? null : current.GameDirectory);
            if (FriendsInstallation.HandOffUninstall(selected))
            {
                Close();
                return;
            }
            busy = true;
            RefreshActions();
            SetStatus("正在卸载好友联机…", false);
            string desktop = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
            await Task.Run(() => FriendsInstallation.Uninstall(FriendsInstallation.Home, desktop, selected));
            current = FriendsInstallation.TryRead(FriendsInstallation.Home);
            SetStatus("好友联机已卸载。", false);
        }
        catch (Exception error)
        {
            current = FriendsInstallation.TryRead(FriendsInstallation.Home);
            SetStatus("卸载提示，请查看弹窗。", true);
            MessageBox.Show(this, error.Message, Program.Title, MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
        finally
        {
            busy = false;
            RefreshActions();
        }
    }
}

internal sealed class FriendsInstallation
{
    internal static readonly string Home = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "BetterMultiplayer", "Friends-Mods");

    private const string ExecutableName = "BetterMultiplayer.exe";
    internal const string SteamExecutableName = "isaac-ng.exe";
    internal const string UninstallerName = "BetterMultiplayer-Uninstall.exe";
    private const string OriginalName = "isaac-ng.bettermultiplayer-original.exe";
    private const string LegacyAssemblyName = "BetterMultiplayer-0.3.0-preview-J511-Friends-Mods-Setup";
    private const string SteamSettingsName = "BetterMultiplayer-installation.txt";
    private const string ChineseCheckMarkerName = "cn_check_bypass.it";
    private const string ShortcutName = "Better Multiplayer 好友联机.lnk";
    private const string Library = FriendsLauncher.Bridge;
    private const string ModSync = FriendsLauncher.ModSync;
    private const string SetupMutex = @"Local\BetterMultiplayer-Friends-Setup";
    private const string GameMutex = @"Local\BetterMultiplayer-Native-P2P-J511";
    private readonly string home, profile;
    internal readonly string GameDirectory;

    private FriendsInstallation(string home, string profile, string gameDirectory)
    {
        this.home = home;
        this.profile = profile;
        GameDirectory = gameDirectory;
    }

    internal string Executable { get { return Path.Combine(home, ExecutableName); } }
    private string TransportDirectory { get { return Path.Combine(home, profile, "transport"); } }
    private string NativeRoot { get { return Path.Combine(home, profile, "Native-P2P"); } }
    private string SteamExecutable { get { return Path.Combine(GameDirectory, SteamExecutableName); } }
    private string OriginalExecutable { get { return Path.Combine(GameDirectory, OriginalName); } }
    private string Uninstaller { get { return Path.Combine(GameDirectory, UninstallerName); } }
    private string ChineseCheckMarker { get { return Path.Combine(GameDirectory, ChineseCheckMarkerName); } }
    private string ChineseCheckMarkerReceipt { get { return Path.Combine(TransportDirectory, ChineseCheckMarkerName); } }
    private static string Settings(string home) { return Path.Combine(home, "installation.txt"); }
    private static string SteamSettings(string game) { return Path.Combine(game, SteamSettingsName); }

    internal static bool HasSteamInstallation(string game)
    {
        return !string.IsNullOrWhiteSpace(game) && game.IndexOfAny(Path.GetInvalidPathChars()) < 0 &&
            File.Exists(SteamSettings(game.Trim()));
    }

    private static string SteamProfile(string game)
    {
        if (!HasSteamInstallation(game))
            return null;
        Guid id;
        if (!Guid.TryParseExact(File.ReadAllText(SteamSettings(game)).Trim(), "N", out id))
            throw new IOException("请检查游戏目录中的 Better Multiplayer 安装记录。");
        return id.ToString("N");
    }

    private static bool IsBetterMultiplayerExe(string path)
    {
        if (!File.Exists(path))
            return false;
        try
        {
            string name = AssemblyName.GetAssemblyName(path).Name;
            return name == Assembly.GetExecutingAssembly().GetName().Name || name == LegacyAssemblyName;
        }
        catch (BadImageFormatException) { return false; }
    }

    internal static FriendsInstallation ReadSteam(string game, string home)
    {
        string profile = SteamProfile(game);
        if (profile == null)
            throw new InvalidOperationException("请打开 Better Multiplayer 安装程序，重新安装 Steam 启动入口。");
        var installation = new FriendsInstallation(home, profile, game);
        if (!File.Exists(installation.OriginalExecutable))
            throw new FileNotFoundException("请检查游戏目录中的原版程序备份。");
        return installation;
    }

    internal static FriendsInstallation TryRead(string home)
    {
        string settings = Settings(home);
        if (!File.Exists(settings))
            return null;

        string[] lines = File.ReadAllLines(settings);
        Guid profile;
        if (lines.Length != 2 || !Guid.TryParseExact(lines[0], "N", out profile) || !Path.IsPathRooted(lines[1]))
            return null;

        var installation = new FriendsInstallation(home, profile.ToString("N"), lines[1]);
        return File.Exists(installation.Executable) &&
            File.Exists(Path.Combine(installation.TransportDirectory, Library)) &&
            File.Exists(Path.Combine(installation.TransportDirectory, ModSync)) ? installation : null;
    }

    internal static FriendsInstallation Read(string home)
    {
        var installation = TryRead(home);
        if (installation == null)
            throw new InvalidOperationException("请打开 Better Multiplayer 安装程序，选择游戏目录并安装。");
        return installation;
    }

    internal bool Matches(string path)
    {
        return string.Equals(GameDirectory.TrimEnd('\\'), path.Trim().TrimEnd('\\'), StringComparison.OrdinalIgnoreCase);
    }

    internal static FriendsInstallation Install(string directory, string home, string desktop)
    {
        string game = Path.GetFullPath(directory);
        if (!SteamGame.IsGameDirectory(game))
            throw new InvalidOperationException("请选择同时包含 isaac-ng.exe 和 steam_api.dll 的游戏目录。");

        using (var mutex = new Mutex(false, SetupMutex))
        using (var launch = new Mutex(false, GameMutex))
        {
            Acquire(mutex, "另一个 Better Multiplayer 安装程序正在运行。");
            try
            {
                Acquire(launch, "请退出以撒后再安装。");
                try
                {
                    EnsureGameClosed();
                    string ownedProfile = SteamProfile(game);
                    var previous = TryRead(home);
                    string profile = ownedProfile ?? (previous != null && previous.Matches(game) ?
                        previous.profile : Guid.NewGuid().ToString("N"));
                    var installation = new FriendsInstallation(home, profile, game);
                    bool replacingEntry = IsBetterMultiplayerExe(installation.SteamExecutable);
                    if ((replacingEntry && (ownedProfile == null || !File.Exists(installation.OriginalExecutable))) ||
                        (File.Exists(installation.OriginalExecutable) && ownedProfile == null) ||
                        (File.Exists(installation.Uninstaller) && !IsBetterMultiplayerExe(installation.Uninstaller)))
                        throw new IOException("请检查游戏目录中的 Better Multiplayer 安装记录和原版程序备份。");

                    Directory.CreateDirectory(installation.TransportDirectory);
                    WriteResource(Library, installation.TransportDirectory);
                    WriteResource(ModSync, installation.TransportDirectory);
                    string source = replacingEntry ? installation.OriginalExecutable : installation.SteamExecutable;
                    new FriendsLauncher(game, source, installation.NativeRoot, installation.TransportDirectory).Prepare(true);

                    string executable = Assembly.GetExecutingAssembly().Location;
                    if (!string.Equals(executable, installation.Executable, StringComparison.OrdinalIgnoreCase))
                        Publish(installation.Executable, path => File.Copy(executable, path));
                    try { CreateShortcut(desktop, installation.Executable); } catch { }
                    bool createdChineseCheckMarker = false;
                    try
                    {
                        createdChineseCheckMarker = installation.CreateChineseCheckMarker();
                        Publish(SteamSettings(game), path => File.WriteAllText(path, profile));
                        Publish(installation.Uninstaller, path => File.Copy(executable, path));
                        Publish(installation.SteamExecutable, path => File.Copy(executable, path),
                            replacingEntry ? null : installation.OriginalExecutable);
                    }
                    catch
                    {
                        if (createdChineseCheckMarker)
                            installation.RemoveChineseCheckMarker();
                        if (ownedProfile == null && !IsBetterMultiplayerExe(installation.SteamExecutable))
                        {
                            File.Delete(installation.Uninstaller);
                            File.Delete(SteamSettings(game));
                        }
                        throw;
                    }
                    Publish(Settings(home), path => File.WriteAllLines(path, new[] { profile, game }));
                    return installation;
                }
                finally { launch.ReleaseMutex(); }
            }
            finally
            {
                mutex.ReleaseMutex();
            }
        }
    }

    internal static bool HandOffUninstall(string game)
    {
        string executable = Application.ExecutablePath;
        string root = Path.GetFullPath(Home).TrimEnd(Path.DirectorySeparatorChar);
        bool gameUninstaller = game != null && string.Equals(executable, Path.Combine(game, UninstallerName),
            StringComparison.OrdinalIgnoreCase);
        if (!gameUninstaller && !executable.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            return false;

        string staging = Path.Combine(Path.GetTempPath(), "BetterMultiplayer-Uninstall-" + Guid.NewGuid().ToString("N"));
        string helper = Path.Combine(staging, ExecutableName);
        Directory.CreateDirectory(staging);
        try
        {
            File.Copy(executable, helper);
            using (var current = Process.GetCurrentProcess())
            using (var worker = Process.Start(helper, "/uninstall " + current.Id + (game == null ? "" : " " + Quote(game))))
            {
                string cleanup = "Wait-Process -Id " + worker.Id +
                    " -ErrorAction SilentlyContinue\n[IO.File]::Delete('" + helper.Replace("'", "''") +
                    "')\n[IO.Directory]::Delete('" + staging.Replace("'", "''") + "')";
                var start = new ProcessStartInfo(PowerShell, "-NoProfile -NonInteractive -EncodedCommand " +
                    Convert.ToBase64String(Encoding.Unicode.GetBytes(cleanup)))
                    { UseShellExecute = false, CreateNoWindow = true };
                try { Process.Start(start).Dispose(); }
                catch (System.ComponentModel.Win32Exception) { }
            }
            return true;
        }
        catch
        {
            File.Delete(helper);
            Directory.Delete(staging);
            throw;
        }
    }

    internal static void WaitForExit(int processId)
    {
        Process process;
        try { process = Process.GetProcessById(processId); }
        catch (ArgumentException) { return; }
        using (process) process.WaitForExit();
    }

    private static void EnsureGameClosed(int ignoreProcess = 0)
    {
        foreach (string name in new[] { "isaac-ng", FriendsLauncher.ProcessName, "isaac-ng.bettermultiplayer-p2p",
            "isaac-ng.bettermultiplayer-1.9.7.19" })
        {
            bool gameOpen = false;
            foreach (var running in Process.GetProcessesByName(name))
            {
                using (running)
                    gameOpen |= running.Id != ignoreProcess;
            }
            if (gameOpen)
                throw new InvalidOperationException("请先退出已打开的以撒游戏。");
        }
    }

    internal static void Uninstall(string home, string desktop, string directory)
    {
        using (var setup = new Mutex(false, SetupMutex))
        using (var game = new Mutex(false, GameMutex))
        {
            Acquire(setup, "请关闭其他 Better Multiplayer 安装窗口，再卸载。");
            try
            {
                Acquire(game, "请退出 Better Multiplayer 游戏，再卸载。");
                try
                {
                    EnsureGameClosed();
                    string root = Path.GetFullPath(home).TrimEnd(Path.DirectorySeparatorChar);
                    var current = TryRead(root);
                    string selected = directory == null ? (current == null ? null : current.GameDirectory) : Path.GetFullPath(directory);
                    string profile = selected == null ? null : SteamProfile(selected);
                    if (profile != null)
                    {
                        var installation = new FriendsInstallation(root, profile, selected);
                        bool activeEntry = IsBetterMultiplayerExe(installation.SteamExecutable);
                        if (File.Exists(installation.OriginalExecutable))
                        {
                            if (activeEntry)
                                File.Replace(installation.OriginalExecutable, installation.SteamExecutable, null);
                            else if (!File.Exists(installation.SteamExecutable))
                                File.Move(installation.OriginalExecutable, installation.SteamExecutable);
                            else
                                File.Delete(installation.OriginalExecutable);
                        }
                        else if (activeEntry)
                            throw new FileNotFoundException("请检查游戏目录中的原版程序备份。");
                        if (IsBetterMultiplayerExe(installation.Uninstaller))
                            File.Delete(installation.Uninstaller);
                        installation.RemoveChineseCheckMarker();
                        File.Delete(SteamSettings(selected));
                    }
                    else if (current != null && selected != null && current.Matches(selected))
                    {
                        profile = current.profile;
                    }

                    if (profile != null)
                    {
                        string profileDirectory = Path.Combine(root, profile);
                        if (Directory.Exists(profileDirectory))
                            RemoveInstallationDirectory(root, profileDirectory);
                    }
                    if (current == null || (selected != null && current.Matches(selected)))
                    {
                        string link = Path.Combine(desktop, ShortcutName);
                        if (File.Exists(link))
                        {
                            dynamic shell = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell"));
                            string target = shell.CreateShortcut(link).TargetPath;
                            if (string.Equals(target, Path.Combine(root, ExecutableName), StringComparison.OrdinalIgnoreCase))
                                File.Delete(link);
                        }
                        File.Delete(Settings(root));
                        string setupExecutable = Path.Combine(root, ExecutableName);
                        if (IsBetterMultiplayerExe(setupExecutable))
                            File.Delete(setupExecutable);
                    }
                }
                finally { game.ReleaseMutex(); }
            }
            finally { setup.ReleaseMutex(); }
        }
    }

    private static void Acquire(Mutex mutex, string message)
    {
        try
        {
            if (!mutex.WaitOne(0))
                throw new InvalidOperationException(message);
        }
        catch (AbandonedMutexException) { }
    }

    private static void RemoveInstallationDirectory(string root, string path)
    {
        path = Path.GetFullPath(path);
        if (!string.Equals(path, root, StringComparison.OrdinalIgnoreCase) &&
            !path.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            throw new IOException(path);

        FileAttributes attributes = File.GetAttributes(path);
        if ((attributes & FileAttributes.Directory) == 0)
        {
            if ((attributes & (FileAttributes.ReadOnly | FileAttributes.ReparsePoint)) == FileAttributes.ReadOnly)
                File.SetAttributes(path, attributes & ~FileAttributes.ReadOnly);
            File.Delete(path);
            return;
        }
        if ((attributes & FileAttributes.ReparsePoint) == 0)
            foreach (string item in Directory.EnumerateFileSystemEntries(path))
                RemoveInstallationDirectory(root, item);
        Directory.Delete(path);
    }

    private static string PowerShell
    {
        get { return Path.Combine(Environment.SystemDirectory, "WindowsPowerShell", "v1.0", "powershell.exe"); }
    }

    internal void Launch(int steamProcess = 0, string[] arguments = null)
    {
        using (var mutex = new Mutex(false, GameMutex))
        {
            Acquire(mutex, "Better Multiplayer 已经在运行。");
            try
            {
                EnsureGameClosed(steamProcess);
                var clients = Process.GetProcessesByName("steam");
                foreach (var client in clients) client.Dispose();
                if (clients.Length == 0)
                    throw new InvalidOperationException("请先登录 Steam。");

                string source = IsBetterMultiplayerExe(SteamExecutable) ? OriginalExecutable : SteamExecutable;
                var launcher = new FriendsLauncher(GameDirectory, source, NativeRoot, TransportDirectory);
                launcher.Prepare(false);
                launcher.Launch(arguments);
            }
            finally { mutex.ReleaseMutex(); }
        }
    }

    internal static string Quote(string value)
    {
        var quoted = new StringBuilder("\"");
        int slashes = 0;
        foreach (char character in value)
        {
            if (character == '\\')
            {
                ++slashes;
                continue;
            }
            quoted.Append('\\', character == '"' ? slashes * 2 + 1 : slashes).Append(character);
            slashes = 0;
        }
        return quoted.Append('\\', slashes * 2).Append('"').ToString();
    }

    private bool CreateChineseCheckMarker()
    {
        if (File.Exists(ChineseCheckMarker))
            return false;

        File.WriteAllText(ChineseCheckMarkerReceipt, Program.Title);
        bool created = false;
        try
        {
            Publish(ChineseCheckMarker, path => File.WriteAllText(path, Program.Title), replaceExisting: false);
            created = true;
            return true;
        }
        catch (IOException)
        {
            if (File.Exists(ChineseCheckMarker))
                return false;
            throw;
        }
        finally
        {
            if (!created)
                File.Delete(ChineseCheckMarkerReceipt);
        }
    }

    private void RemoveChineseCheckMarker()
    {
        if (!File.Exists(ChineseCheckMarkerReceipt))
            return;
        if (File.Exists(ChineseCheckMarker) && File.ReadAllText(ChineseCheckMarker) == Program.Title)
            File.Delete(ChineseCheckMarker);
        File.Delete(ChineseCheckMarkerReceipt);
    }

    private static void WriteResource(string name, string directory)
    {
        Publish(Path.Combine(directory, name), path =>
        {
            using (var source = Assembly.GetExecutingAssembly().GetManifestResourceStream(name))
            using (var destination = File.Create(path))
                source.CopyTo(destination);
        });
    }

    private static void ClearReadOnly(string path)
    {
        if (string.IsNullOrEmpty(path) || !File.Exists(path))
            return;
        try
        {
            FileAttributes attrs = File.GetAttributes(path);
            if ((attrs & FileAttributes.ReadOnly) != 0)
                File.SetAttributes(path, attrs & ~FileAttributes.ReadOnly);
        }
        catch { }
    }

    internal static void Publish(string destination, Action<string> write, string backup = null, bool replaceExisting = true)
    {
        string temporary = destination + "." + Guid.NewGuid().ToString("N");
        try
        {
            write(temporary);
            ClearReadOnly(destination);
            ClearReadOnly(backup);
            if (replaceExisting && File.Exists(destination))
                File.Replace(temporary, destination, backup);
            else
                File.Move(temporary, destination);
        }
        finally
        {
            if (File.Exists(temporary))
                File.Delete(temporary);
        }
    }

    private static void CreateShortcut(string desktop, string executable)
    {
        dynamic shell = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell"));
        dynamic shortcut = shell.CreateShortcut(Path.Combine(desktop, ShortcutName));
        shortcut.TargetPath = executable;
        shortcut.Arguments = "/play";
        shortcut.WorkingDirectory = Path.GetDirectoryName(executable);
        shortcut.Description = "启动 Better Multiplayer 好友联机";
        shortcut.Save();
    }
}

internal static class SteamGame
{
    private static readonly Regex LibraryPath = new Regex("(?im)^\\s*\"path\"\\s*\"([^\"]+)\"");
    private static readonly Regex InstallDirectory = new Regex("(?im)^\\s*\"installdir\"\\s*\"([^\"]+)\"");

    internal static bool IsGameDirectory(string directory)
    {
        return File.Exists(Path.Combine(directory, "isaac-ng.exe")) &&
            File.Exists(Path.Combine(directory, "steam_api.dll"));
    }

    internal static string Find()
    {
        try
        {
            using (var steam = Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam"))
            {
                string root = steam == null ? null : steam.GetValue("SteamPath") as string;
                if (string.IsNullOrWhiteSpace(root))
                    root = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Steam");

                string candidate = FindInLibrary(root);
                if (candidate != null)
                    return candidate;

                string libraryList = Path.Combine(root, "steamapps", "libraryfolders.vdf");
                if (File.Exists(libraryList))
                {
                    foreach (Match entry in LibraryPath.Matches(File.ReadAllText(libraryList)))
                    {
                        try
                        {
                            candidate = FindInLibrary(entry.Groups[1].Value.Replace(@"\\", @"\"));
                            if (candidate != null)
                                return candidate;
                        }
                        catch { }
                    }
                }
                return "";
            }
        }
        catch
        {
            return "";
        }
    }

    private static string FindInLibrary(string library)
    {
        string apps = Path.Combine(library, "steamapps");
        string manifest = Path.Combine(apps, "appmanifest_250900.acf");
        if (!File.Exists(manifest))
            return null;

        var folder = InstallDirectory.Match(File.ReadAllText(manifest));
        if (!folder.Success)
            return null;
        string directory = Path.GetFullPath(Path.Combine(apps, "common", folder.Groups[1].Value));
        return IsGameDirectory(directory) ? directory : null;
    }
}
