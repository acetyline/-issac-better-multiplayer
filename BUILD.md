# 构建 Better Multiplayer 好友版

使用 Windows x64、PowerShell 7、Windows 的 .NET Framework 4.x C# 编译器，以及 [Zig 0.15.2](https://ziglang.org/download/#release-0.15.2)。

在仓库根目录用 PowerShell 7 执行，按本机位置填写 `zig.exe` 路径。

```powershell
.\Build-FriendsSetup.ps1 -ZigPath 'C:\Tools\zig-x86_64-windows-0.15.2\zig.exe'
```

构建脚本会编译联机 DLL、Mod 同步助手和安装器，并将组件打进安装 EXE。安装 EXE 与好友版 ZIP 都生成在 `dist/Friends-Only/`。编译使用本仓库的源码和上述工具。安装及运行使用玩家本机的 Steam 游戏。

## 通过标签发布

将含有[发布工作流](.github/workflows/release-j511.yml)的提交推到启用 Actions 的 GitHub 仓库。在要发布的提交上创建并推送标签。

```powershell
git tag v0.4.0
git push origin v0.4.0
```

该标签触发 Windows 构建。工作流使用 GitHub 自带的 `GITHUB_TOKEN` 创建预览版 Release，并附上生成的安装 EXE 和对应的构建记录链接。
