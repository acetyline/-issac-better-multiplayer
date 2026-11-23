# Better Multiplayer J511 好友联机预览版

适用于 Windows Steam 版《以撒的结合：忏悔+》1.9.7.19.J511。使用 Steam 好友房联机，以房主为准同步已启用的 Mod 和支持的设置。失步时可尝试回到房主的房间或楼层检查点。

## 安装与联机

在仓库的 **Releases** 下载 `BetterMultiplayer-0.3.0-preview-J511-Friends-Mods-Setup.exe`。房主和好友各自运行同一版安装程序并选择游戏目录。从 Steam 启动游戏后，进入 **Multiplayer → Friend Match** 建房或加入。窗口标题会显示 **(better multiplayer)**。

退出游戏后，运行游戏目录中的 `BetterMultiplayer-Uninstall.exe` 可恢复 Steam 的原版启动入口。更多用法见[好友版说明](transport/README-FriendsMods.md)。

## 源码与运行行为

- [安装器](installer/FriendsSetup.cs)备份原始游戏入口并创建卸载程序。配套组件和联机日志存放在 `%LOCALAPPDATA%\BetterMultiplayer\Friends-Mods`。
- [启动脚本](transport/Start-NativeP2P.ps1)使用玩家本机的游戏文件生成联机副本，并为好友版选择 Steam 好友联机。
- [联机源码](transport/)通过 Steam 传输游戏数据和房主检查点。Mod 同步传输已启用的 Mod 清单、工坊 ID 和支持的配置，包括 `config.json`、`settings.json`、`options.json` 及 Mod Config Menu 设置。

[构建说明](BUILD.md)列出本地编译和版本发布步骤。预览版 Release 附有生成该安装 EXE 的 Actions 运行记录。
