using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;
using System.Xml;
using System.Xml.Linq;

namespace BetterMultiplayer
{
    public sealed class ModProfile
    {
        public bool Enabled;
        public List<ModEntry> Mods = new List<ModEntry>();
    }

    public sealed class ModEntry
    {
        public string Id, Folder, Version, Name;
        public SortedDictionary<string, string> Settings = new SortedDictionary<string, string>(StringComparer.Ordinal);
    }

    internal sealed class ModSync
    {
        private const int MaxManifest = 48 * 1024;
        private const int MaxSettings = 64 * 1024;
        private const string MenuId = "2494192799";
        private const string MenuKey = "ModConfigSave";
        private static readonly string[] ConfigFiles = { "config.json", "settings.json", "options.json" };
        private static readonly string[] Slots = { "save1.dat", "save2.dat", "save3.dat" };
        private static readonly UTF8Encoding Utf8 = new UTF8Encoding(false, true);
        private static readonly Regex GoodTripName = new Regex(@"good\s*trip", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
        private static readonly Regex DisplayWhitespace = new Regex(@"[\p{Cc}\p{Cf}\s]+");
        private readonly string mods, data, session;
        private readonly JavaScriptSerializer json = new JavaScriptSerializer { MaxJsonLength = MaxSettings, RecursionLimit = 32 };

        private sealed class Installed
        {
            internal ModEntry Entry;
            internal string Path;
        }

        private ModSync(string game, string session)
        {
            mods = Path.Combine(game, "mods");
            data = Path.Combine(game, "data");
            this.session = session;
        }

        private static int Main(string[] args)
        {
            Console.OutputEncoding = Utf8;
            try
            {
                if (args.Length != 4)
                    throw new IOException("请从 Better Multiplayer 启动 Mod 同步。");
                var sync = new ModSync(args[1], args[2]);
                if (args[0] == "prepare")
                    sync.Prepare();
                else if (args[0] == "scan" && (args[3] == "0" || args[3] == "1"))
                    sync.Export(args[3] == "1");
                else if (args[0] == "apply")
                    Console.WriteLine(sync.Apply() ? "1" : "0");
                else
                    throw new IOException("请核对 Mod 同步操作。");
                return 0;
            }
            catch (Exception error)
            {
                Console.Error.WriteLine(error.Message);
                return 1;
            }
        }

        private List<Installed> InstalledMods(List<string> blocked = null)
        {
            RequirePlain(mods);
            var installed = new List<Installed>();
            if (Directory.Exists(mods))
            {
                foreach (string path in Directory.EnumerateDirectories(mods).Where(Plain))
                {
                    var mod = ReadMod(path, null, blocked);
                    if (mod != null)
                        installed.Add(mod);
                }
            }
            return installed;
        }

        private static XElement ReadMetadata(string file)
        {
            var parser = new XmlReaderSettings { DtdProcessing = DtdProcessing.Prohibit, XmlResolver = null, MaxCharactersInDocument = 1024 * 1024 };
            using (var reader = XmlReader.Create(file, parser))
                return XElement.Load(reader);
        }

        private static Installed ReadMod(string path, string workshop, List<string> blocked = null)
        {
            string actualFolder = Path.GetFileName(path);
            if (GoodTripName.IsMatch(actualFolder))
            {
                if (blocked != null) blocked.Add(actualFolder);
                return null;
            }
            string file = Path.Combine(path, "metadata.xml");
            if (!File.Exists(file) || !Plain(file))
                return null;
            var xml = ReadMetadata(file);
            if (xml.Name != "metadata")
                return null;

            string folder = ((string)xml.Element("directory") ?? actualFolder).Trim().ToLowerInvariant();
            if (GoodTripName.IsMatch((string)xml.Element("name") ?? "") || GoodTripName.IsMatch(folder))
            {
                if (blocked != null) blocked.Add(actualFolder);
                return null;
            }
            if (!SafeFolder(folder))
                throw new IOException("请核对 Mod 目录名「" + folder + "」。");
            string id = ((string)xml.Element("id") ?? "").Trim();
            string version = ((string)xml.Element("version") ?? "").Trim();
            if (version.Length > 120)
                throw new IOException("请核对 Mod 版本「" + folder + "」。");
            if (workshop != null && WorkshopId(id) && workshop != id)
                return null;
            return new Installed
            {
                Path = path,
                Entry = new ModEntry { Id = workshop ?? (WorkshopId(id) ? id : "local:" + folder),
                    Folder = folder, Version = version, Name = DisplayName((string)xml.Element("name"), folder) }
            };
        }

        private static string DisplayName(string value, string folder)
        {
            string name = DisplayWhitespace.Replace(value ?? "", " ").Trim();
            if (name.Length > 100)
            {
                int end = Char.IsHighSurrogate(name[99]) ? 99 : 100;
                name = name.Substring(0, end).TrimEnd();
            }
            return name.Length == 0 ? folder : name;
        }

        private void Prepare()
        {
            var blocked = new List<string>();
            var installed = InstalledMods(blocked);
            Publish(Path.Combine(session, "blocked-mods.txt"), String.Join("\n", blocked), Utf8);
            PublishInstalled(installed);
        }

        private void PublishInstalled(List<Installed> installed)
        {
            Publish(Path.Combine(session, "installed.txt"), String.Join(",", installed.Select(item => item.Entry.Id)
                .Where(WorkshopId).Distinct(StringComparer.Ordinal)), Utf8);
        }

        private void Export(bool enabled)
        {
            var installed = InstalledMods();
            var profile = new ModProfile { Enabled = enabled };
            if (profile.Enabled)
            {
                foreach (var item in installed.Where(item => !File.Exists(Path.Combine(item.Path, "disable.it")))
                                              .OrderBy(item => item.Entry.Id, StringComparer.Ordinal))
                {
                    var mod = item.Entry;
                    if (profile.Mods.Any(previous => previous.Id == mod.Id || previous.Folder == mod.Folder))
                        throw new IOException("发现重复的启用 Mod「" + mod.Folder + "」。");
                    ReadSettings(mod);
                    profile.Mods.Add(mod);
                }
            }
            PublishInstalled(installed);
            Publish(Path.Combine(session, "local.txt"), Encode(profile), Utf8);
        }

        private void ReadSettings(ModEntry mod)
        {
            string folder = SettingsDirectory(mod);
            foreach (string name in ConfigFiles)
            {
                string file = Path.Combine(folder, name);
                if (File.Exists(file))
                    mod.Settings[name] = Canonical(ReadJson(file));
            }
            if (mod.Id != MenuId || mod.Folder != "mod_config_menu_cn")
                return;
            foreach (string slot in Slots)
            {
                var settings = ReadObject(Path.Combine(folder, slot));
                object value;
                if (settings.TryGetValue(MenuKey, out value) && value is IDictionary<string, object>)
                    mod.Settings[slot] = Canonical(value);
            }
        }

        private string Encode(ModProfile profile)
        {
            var workshop = profile.Mods.Where(mod => WorkshopId(mod.Id)).ToList();
            string wire = "BM2\n" + String.Join(",", workshop.Select(mod => mod.Id)) + "\n" +
                String.Join("\t", workshop.Select(mod => mod.Name)) + "\n" +
                Convert.ToBase64String(Utf8.GetBytes(json.Serialize(profile)));
            if (profile.Mods.Count > 256 || Utf8.GetByteCount(wire) > MaxManifest)
                throw new IOException("请精简本房的 Mod 设置后重新建房。");
            return wire;
        }

        private ModProfile Decode(string wire)
        {
            var lines = wire.Split('\n');
            if (Utf8.GetByteCount(wire) > MaxManifest || lines.Length != 4 || lines[0] != "BM2")
                throw new IOException("请与房主使用同一版好友联机安装包。");
            var profile = json.Deserialize<ModProfile>(Utf8.GetString(Convert.FromBase64String(lines[3])));
            if (profile == null || profile.Mods == null || profile.Mods.Count > 256 || !profile.Enabled && profile.Mods.Count > 0)
                throw new IOException("请房主重新创建好友房。");

            var ids = new HashSet<string>(StringComparer.Ordinal);
            var folders = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var mod in profile.Mods)
            {
                if (mod == null || !SafeFolder(mod.Folder) || GoodTripName.IsMatch(mod.Folder) ||
                    mod.Version == null || mod.Version.Length > 120 || mod.Name != DisplayName(mod.Name, mod.Folder) ||
                    (!WorkshopId(mod.Id) && mod.Id != "local:" + mod.Folder) ||
                    !ids.Add(mod.Id) || !folders.Add(mod.Folder) || mod.Settings == null)
                    throw new IOException("请房主核对启用的 Mod 清单。");
                foreach (var file in mod.Settings)
                {
                    bool menu = mod.Id == MenuId && mod.Folder == "mod_config_menu_cn" && Slots.Contains(file.Key);
                    if ((!menu && !ConfigFiles.Contains(file.Key)) || file.Value == null || file.Value.Length > MaxSettings)
                        throw new IOException("请房主核对 Mod 设置「" + mod.Folder + "」。");
                    object parsed = json.DeserializeObject(file.Value);
                    if (menu && !(parsed is IDictionary<string, object>))
                        throw new IOException("请房主核对 Mod Config Menu 设置。");
                }
            }
            var workshop = profile.Mods.Where(mod => WorkshopId(mod.Id)).ToList();
            if (lines[1] != String.Join(",", workshop.Select(mod => mod.Id)) ||
                lines[2] != String.Join("\t", workshop.Select(mod => mod.Name)))
                throw new IOException("请房主刷新 Mod 清单后重新邀请。");
            return profile;
        }

        private static bool WorkshopId(string id)
        {
            ulong parsed;
            return UInt64.TryParse(id, NumberStyles.None, CultureInfo.InvariantCulture, out parsed) && parsed > 0 &&
                id == parsed.ToString(CultureInfo.InvariantCulture);
        }

        private static bool SafeFolder(string name)
        {
            return !String.IsNullOrWhiteSpace(name) && name.Length <= 150 && name != "." && name != ".." &&
                name.IndexOfAny(Path.GetInvalidFileNameChars()) < 0 && !name.EndsWith(".") && !name.EndsWith(" ") &&
                !Regex.IsMatch(name, @"^(?:CON|PRN|AUX|NUL|COM[0-9¹²³]|LPT[0-9¹²³])(?:\.|$)", RegexOptions.IgnoreCase);
        }

        private string SettingsDirectory(ModEntry mod)
        {
            RequirePlain(data);
            string folder = Path.Combine(data, mod.Folder);
            RequirePlain(folder);
            return folder;
        }

        private object ReadJson(string path)
        {
            RequirePlain(path);
            if (new FileInfo(path).Length > MaxSettings)
                throw new IOException("请核对 Mod 设置文件大小「" + Path.GetFileName(path) + "」。");
            return json.DeserializeObject(File.ReadAllText(path, Utf8));
        }

        private IDictionary<string, object> ReadObject(string path)
        {
            if (!File.Exists(path))
                return new Dictionary<string, object>(StringComparer.Ordinal);
            var value = ReadJson(path) as IDictionary<string, object>;
            if (value == null)
                throw new IOException("请核对 Mod Config Menu 存档格式。");
            return value;
        }

        private string Canonical(object value)
        {
            var dictionary = value as IDictionary<string, object>;
            if (dictionary != null)
                return "{" + String.Join(",", dictionary.OrderBy(pair => pair.Key, StringComparer.Ordinal)
                    .Select(pair => json.Serialize(pair.Key) + ":" + Canonical(pair.Value))) + "}";
            var list = value as IList;
            return list == null ? json.Serialize(value) : "[" + String.Join(",", list.Cast<object>().Select(Canonical)) + "]";
        }

        private static bool Plain(string path)
        {
            return (File.GetAttributes(path) & FileAttributes.ReparsePoint) == 0;
        }

        private static void RequirePlain(string path)
        {
            if ((File.Exists(path) || Directory.Exists(path)) && !Plain(path))
                throw new IOException("请核对 Mod 文件路径「" + Path.GetFileName(path) + "」。");
        }

        private static void Publish(string path, string contents, Encoding encoding)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            string temporary = path + "." + Guid.NewGuid().ToString("N");
            try
            {
                File.WriteAllText(temporary, contents, encoding);
                if (File.Exists(path))
                    File.Replace(temporary, path, null);
                else
                    File.Move(temporary, path);
            }
            finally
            {
                if (File.Exists(temporary))
                    File.Delete(temporary);
            }
        }

        private bool Apply()
        {
            var profile = Decode(File.ReadAllText(Path.Combine(session, "host.txt"), Utf8));
            var locations = File.ReadAllLines(Path.Combine(session, "items.txt"), Utf8)
                .Select(line => line.Split(new[] { '\t' }, 2)).ToDictionary(pair => pair[0], pair => pair[1], StringComparer.Ordinal);
            var installed = InstalledMods();
            var chosen = new Dictionary<string, string>(StringComparer.Ordinal);
            var actions = new List<Action>();
            var undo = new Stack<Action>();
            string backup = Path.Combine(Path.GetDirectoryName(mods), "BetterMultiplayerBackups");
            RequirePlain(backup);
            backup = Path.Combine(backup, "mod-sync-" + Guid.NewGuid().ToString("N"));

            foreach (var mod in profile.Mods)
            {
                var matching = installed.Where(item => item.Entry.Id == mod.Id).ToList();
                if (matching.Count > 1)
                    throw new IOException("请清理重复的 Mod「" + mod.Folder + "」。");
                var existing = matching.SingleOrDefault();
                string target = existing == null ? Path.Combine(mods, mod.Folder + "_" + mod.Id) : existing.Path;
                if (existing == null || existing.Entry.Version != mod.Version || existing.Entry.Folder != mod.Folder)
                {
                    string source;
                    if (!WorkshopId(mod.Id) || !locations.TryGetValue(mod.Id, out source) || !Directory.Exists(source) || !Plain(source))
                        throw new IOException("请与房主对齐本地 Mod 或工坊版本「" + mod.Folder + "」。");
                    var downloaded = ReadMod(source, mod.Id);
                    if (downloaded == null || downloaded.Entry.Folder != mod.Folder || downloaded.Entry.Version != mod.Version ||
                        existing == null && Directory.Exists(target))
                        throw new IOException("请与房主核对工坊版本「" + mod.Folder + "」。");
                    var staged = Path.Combine(backup, "new-" + mod.Id);
                    CopyWorkshop(source, staged);
                    string metadata = Path.Combine(staged, "metadata.xml");
                    var document = ReadMetadata(metadata);
                    if ((string)document.Element("id") != mod.Id)
                    {
                        document.SetElementValue("id", mod.Id);
                        document.Save(metadata);
                    }
                    var old = Path.Combine(backup, "old-" + mod.Id);
                    actions.Add(() =>
                    {
                        Directory.CreateDirectory(mods);
                        if (Directory.Exists(target))
                        {
                            Directory.Move(target, old);
                            undo.Push(() => Directory.Move(old, target));
                        }
                        Directory.Move(staged, target);
                        undo.Push(() => Directory.Move(target, staged));
                    });
                }
                chosen.Add(target, mod.Id);
                var flag = Path.Combine(target, "disable.it");
                actions.Add(() => ChangeFile(flag, null, Utf8, backup, undo));
                PlanSettings(mod, actions, backup, undo);
            }
            if (profile.Enabled)
            {
                foreach (var extra in installed.Where(item => !chosen.ContainsKey(item.Path)))
                {
                    var flag = Path.Combine(extra.Path, "disable.it");
                    if (!File.Exists(flag))
                        actions.Add(() => ChangeFile(flag, "", Utf8, backup, undo));
                }
            }
            try
            {
                foreach (var change in actions)
                    change();
                Export(profile.Enabled);
                if (File.ReadAllText(Path.Combine(session, "local.txt"), Utf8) != Encode(profile))
                    throw new IOException("请核对同步后的 Mod 组合。");
            }
            catch
            {
                while (undo.Count > 0)
                    undo.Pop()();
                throw;
            }
            return profile.Enabled;
        }

        private void PlanSettings(ModEntry mod, List<Action> actions, string backup, Stack<Action> undo)
        {
            string folder = SettingsDirectory(mod);
            foreach (string name in ConfigFiles)
            {
                string path = Path.Combine(folder, name), value;
                mod.Settings.TryGetValue(name, out value);
                if (value != null || File.Exists(path))
                    actions.Add(() => ChangeFile(path, value, Utf8, backup, undo));
            }
            if (mod.Id != MenuId || mod.Folder != "mod_config_menu_cn")
                return;
            foreach (string slot in Slots)
            {
                string path = Path.Combine(folder, slot), value;
                var record = ReadObject(path);
                if (mod.Settings.TryGetValue(slot, out value))
                    record[MenuKey] = json.DeserializeObject(value);
                else if (!record.Remove(MenuKey))
                    continue;
                string contents = Canonical(record);
                actions.Add(() => ChangeFile(path, contents, Utf8, backup, undo));
            }
        }

        private static void ChangeFile(string path, string value, Encoding encoding, string backup, Stack<Action> undo)
        {
            RequirePlain(path);
            bool exists = File.Exists(path);
            if (!exists && value == null)
                return;
            if (exists && value != null && String.Equals(encoding.GetString(File.ReadAllBytes(path)), value, StringComparison.Ordinal))
                return;
            if (exists)
            {
                Directory.CreateDirectory(backup);
                string original = Path.Combine(backup, Guid.NewGuid().ToString("N") + "-" + Path.GetFileName(path));
                File.Copy(path, original);
                undo.Push(() => File.Copy(original, path, true));
            }
            else
                undo.Push(() => File.Delete(path));
            if (value == null)
                File.Delete(path);
            else
                Publish(path, value, encoding);
        }

        private static void CopyWorkshop(string source, string target)
        {
            RequirePlain(source);
            Directory.CreateDirectory(target);
            foreach (var path in Directory.GetFiles(source))
            {
                RequirePlain(path);
                var name = Path.GetFileName(path);
                if (!name.Equals("disable.it", StringComparison.OrdinalIgnoreCase))
                    File.Copy(path, Path.Combine(target, name));
            }
            foreach (var path in Directory.GetDirectories(source))
                CopyWorkshop(path, Path.Combine(target, Path.GetFileName(path)));
        }
    }
}
