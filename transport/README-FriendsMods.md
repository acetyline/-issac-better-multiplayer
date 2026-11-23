# Better Multiplayer 0.3.0 好友 Mod 预览版

在 Windows Steam 版 Repentance+ 1.9.7.19.J511 的好友房使用游戏里已启用的 Mod。

## 启动

双方运行安装 EXE 并选择游戏目录。之后从 Steam 点击「开始游戏」即可启动好友版，窗口标题会显示 **(better multiplayer)**。在 **Multiplayer → Friend Match** 建房或加入。

好友联机版固定使用 Steam 好友房。换房主后，从新房间发一条 Steam 邀请。

手动指定游戏目录时，在解压目录运行以下命令。

```powershell
.\Install.ps1 -GameDirectory 'D:\SteamLibrary\steamapps\common\The Binding of Isaac Rebirth' -FriendsOnly
```

## 失步恢复

双方用同一版好友包启动。失步后，房主在原对局继续，客机确认游戏显示的加入提示，也可从 **Friend Match** 重新加入房主。客机回房后，房主清完当前房间即可同步房主的房间检查点，一起继续这一局。下楼时也会同步楼层检查点。

## Mod 同步

房主在建房前选好 Mod 和配置。好友进房后，Steam 会订阅和下载缺少的工坊 Mod。游戏会对齐启用列表、Mod Config Menu 中文版的设置，以及 Mod 数据目录里的 config.json、settings.json 和 options.json。有变更时，客机会重启游戏并尝试回到原房。私人制作的本地 Mod 由各位玩家提前安装。

启用角色 Mod 后，继续在好友房选角菜单向左或右切换即可找到自定义角色，普通和里角色沿用原菜单切换键。好友房沿用单人角色的肖像资源。开局前会核对双方加载的角色列表。

## GoodTrip

双方启用工坊的 GoodTrip。房主长按地图键打开 GoodTrip 地图，用单人模式的操作选择目的地，队伍随房主一起传送。

## 卸载

退出游戏，双击游戏目录中的 **BetterMultiplayer-Uninstall.exe** 并确认。Steam 恢复启动原版游戏。

联机记录位于 `%LOCALAPPDATA%\BetterMultiplayer\Friends-Mods` 下对应安装目录的 `Native-P2P\J511-Friends\transport.log`。
