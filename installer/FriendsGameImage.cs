using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

internal sealed class FriendsGameImage
{
    private static readonly string[] SteamImports = {
        "SteamAPI_GetHSteamUser", "SteamAPI_RegisterCallback", "SteamAPI_RegisterCallResult",
        "SteamAPI_RunCallbacks", "SteamAPI_Shutdown", "SteamAPI_UnregisterCallback",
        "SteamAPI_UnregisterCallResult", "SteamInternal_ContextInit",
        "SteamInternal_FindOrCreateUserInterface", "SteamInternal_SteamAPI_Init"
    };

    private readonly byte[] bytes, legacy, alternateBootstrap;
    private readonly bool j514;
    private readonly int sections, sectionCount, bootstrapOffset;
    internal readonly bool NeedsChineseBootstrap;
    internal string Build { get { return j514 ? "J514" : "J511"; } }

    internal FriendsGameImage(string executable, byte[] steam)
    {
        bytes = File.ReadAllBytes(executable);
        j514 = IsBuild(bytes, 9539592, 0x140, 0x6b0775bf);
        if ((!j514 && !IsBuild(bytes, 9537032, 0x140, 0x6b01d5e1)) ||
            !IsBuild(steam, 266336, 0x108, 0x672a8ef8))
            throw new IOException("当前启动配置适用于 Windows Steam 版 Repentance+ 1.9.7.19.J511 和 1.9.7.20.J514。");

        int header = BitConverter.ToInt32(bytes, 0x3c);
        int optional = header + 24;
        sections = optional + BitConverter.ToUInt16(bytes, header + 20);
        sectionCount = BitConverter.ToUInt16(bytes, header + 6);
        if (sectionCount == 0 || sections + 40 * sectionCount > bytes.Length)
            throw InvalidImage();
        RedirectSteam(Offset(BitConverter.ToUInt32(bytes, optional + 104), 20));

        bootstrapOffset = Offset(Rva(0x7a1694, 0x7a2754), 8);
        string bootstrap = Encoding.ASCII.GetString(bytes, bootstrapOffset, 8);
        NeedsChineseBootstrap = bootstrap == "bootstp\0";
        if (NeedsChineseBootstrap || bootstrap == "userenv\0")
            alternateBootstrap = Encoding.ASCII.GetBytes(NeedsChineseBootstrap ? "userenv\0" : "bootstp\0");

        // The Mod prompts and recovery calls must match the shipped game.
        Expect(Rva(0x50961f, 0x509b0f), j514 ? 0x020a7ce8u : 0x020a5ce8u);
        Expect(Rva(0x5098da, 0x509dca), 0x00ccd1e8);
        Expect(Rva(0x509c7a, 0x50a16a), 0x00c931e8);
        Replace(Rva(0x521108, 0x521618), new byte[] { 0x74, 0x2a }, new byte[] { 0x74, 0x0a });
        Replace(Rva(0x521112, 0x521622), new byte[] { 0x74, 0x12 }, new byte[] { 0x74, 0 });
        legacy = (byte[])bytes.Clone();

        Replace(Rva(0x53abc8, 0x53b0e8),
            new byte[] { 0x8b, 0x75, 0x08, 0x89, 0xb1, 0xc0, 0, 0, 0 }, new byte[] { 0x6a, 0x01, 0x5e });
        Replace(Rva(0x59a2fe, 0x59a96e),
            new byte[] { 0x0f, 0x85, 0x8f, 0, 0, 0 }, new byte[] { 0xe9, 0xd8, 0, 0, 0, 0x90 });
        Replace(Rva(0x643014, 0x643684), new byte[] { 0x74, 0x45, 0x80, 0x0d }, new byte[] { 0xeb, 0x45 });
        Replace(Rva(0x66fe50, 0x6704a0),
            new byte[] { 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68 }, new byte[] { 0xc2, 0x04, 0 });
    }

    private static bool IsBuild(byte[] image, int size, int header, uint stamp)
    {
        return image.Length == size && BitConverter.ToUInt16(image, 0) == 0x5a4d &&
            BitConverter.ToInt32(image, 0x3c) == header && BitConverter.ToUInt32(image, header) == 0x4550 &&
            BitConverter.ToUInt16(image, header + 4) == 0x14c &&
            BitConverter.ToUInt16(image, header + 24) == 0x10b &&
            BitConverter.ToUInt32(image, header + 8) == stamp;
    }

    private uint Rva(uint older, uint newer) { return j514 ? newer : older; }

    private IOException InvalidImage()
    {
        return new IOException("请通过 Steam 验证 Repentance+ " + Build + " 的安装文件。");
    }

    private int Offset(uint rva, int length)
    {
        for (int index = 0; index < sectionCount; index++)
        {
            int section = sections + 40 * index;
            uint address = BitConverter.ToUInt32(bytes, section + 12);
            uint size = BitConverter.ToUInt32(bytes, section + 16);
            if (rva < address || (ulong)(rva - address) + (uint)length > size)
                continue;
            ulong offset = (ulong)BitConverter.ToUInt32(bytes, section + 20) + rva - address;
            if (offset + (uint)length <= (uint)bytes.Length)
                return (int)offset;
            break;
        }
        throw InvalidImage();
    }

    private string ImportName(int offset)
    {
        int end = Array.IndexOf(bytes, (byte)0, offset, Math.Min(129, bytes.Length - offset));
        if (end < 0)
            throw InvalidImage();
        return Encoding.ASCII.GetString(bytes, offset, end - offset);
    }

    private void RedirectSteam(int imports)
    {
        int steam = -1;
        for (int index = 0; index < 64; index++)
        {
            int descriptor = imports + index * 20;
            if (descriptor + 20 > bytes.Length)
                throw InvalidImage();
            uint name = BitConverter.ToUInt32(bytes, descriptor + 12);
            if (name == 0)
                break;
            int target = Offset(name, 1);
            if (!String.Equals(ImportName(target), "steam_api.dll", StringComparison.OrdinalIgnoreCase))
                continue;
            if (steam >= 0)
                throw InvalidImage();
            steam = target;
            uint lookup = BitConverter.ToUInt32(bytes, descriptor);
            if (lookup == 0)
                lookup = BitConverter.ToUInt32(bytes, descriptor + 16);
            var expected = new HashSet<string>(SteamImports, StringComparer.Ordinal);
            for (int entry = 0; entry <= SteamImports.Length; entry++)
            {
                uint import = BitConverter.ToUInt32(bytes, Offset(lookup + (uint)(entry * 4), 4));
                if (import == 0)
                    break;
                if ((import & 0x80000000) != 0 || !expected.Remove(ImportName(Offset(import, 3) + 2)))
                    throw InvalidImage();
            }
            if (expected.Count != 0)
                throw InvalidImage();
        }
        if (steam < 0)
            throw InvalidImage();
        Encoding.ASCII.GetBytes("bm_steam.dll\0").CopyTo(bytes, steam);
    }

    private void Expect(uint rva, uint expected)
    {
        if (BitConverter.ToUInt32(bytes, Offset(rva, 4)) != expected)
            throw InvalidImage();
    }

    private void Replace(uint rva, byte[] expected, byte[] replacement)
    {
        int offset = Offset(rva, expected.Length);
        for (int index = 0; index < expected.Length; index++)
            if (bytes[offset + index] != expected[index])
                throw InvalidImage();
        replacement.CopyTo(bytes, offset);
    }

    internal static bool Matches(string path, byte[] expected)
    {
        return File.Exists(path) && Matches(File.ReadAllBytes(path), expected);
    }

    private static bool Matches(byte[] actual, byte[] expected)
    {
        if (actual.Length != expected.Length)
            return false;
        for (int index = 0; index < actual.Length; index++)
            if (actual[index] != expected[index])
                return false;
        return true;
    }

    internal bool Matches(string path) { return Matches(path, bytes); }

    internal void Publish(string path)
    {
        if (File.Exists(path))
        {
            byte[] actual = File.ReadAllBytes(path);
            if (Matches(actual, bytes))
                return;
            bool replace = Matches(actual, legacy);
            if (!replace && actual.Length == bytes.Length && alternateBootstrap != null)
            {
                bool matchesBootstrap = true;
                for (int index = 0; index < alternateBootstrap.Length; index++)
                    matchesBootstrap &= actual[bootstrapOffset + index] == alternateBootstrap[index];
                if (matchesBootstrap)
                {
                    Array.Copy(bytes, bootstrapOffset, actual, bootstrapOffset, alternateBootstrap.Length);
                    replace = Matches(actual, bytes) || Matches(actual, legacy);
                }
            }
            if (!replace)
                throw new IOException("请先把早期联机副本移入备份目录。");
        }
        FriendsInstallation.Publish(path, temporary => File.WriteAllBytes(temporary, bytes));
    }
}
