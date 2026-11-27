# Better Multiplayer 0.4.0 好友 Mod 预览版

支持 Windows Steam 版 Repentance+ 1.9.7.19.J511 和 1.9.7.20.J514。房间成员须使用相同的游戏版本和联机客户端。

## 启动

双方运行同一版安装 EXE 并选择游戏目录。之后从 Steam 点击「开始游戏」即可启动 Better Multiplayer，窗口标题会显示 **(better multiplayer)**。

安装 EXE 和 Steam 启动入口会直接准备联机副本并启动游戏。

若 Steam 更新后恢复了原版启动入口，运行新版 Better Multiplayer 安装器即可重新接入。

可以建立公开房，也可以继续通过 Steam 好友邀请联机。公开房列表显示版本相同、可加入且还有空位的 Better Multiplayer 房间。

手动指定游戏目录时，在解压目录运行以下命令。

```powershell
.\Install.ps1 -GameDirectory 'D:\SteamLibrary\steamapps\common\The Binding of Isaac Rebirth' -FriendsOnly
```

## 中文补丁

好友版沿用游戏本身的语言与联机页面文案。需要《忏悔+》中文时，可单独安装与游戏版本匹配的汉化补丁。

已经装好与游戏版本匹配的中文补丁的玩家可以直接运行好友版安装器。安装器会保留打过汉化的游戏本体，并让联机副本继续使用现有的 `bootstp.dll`。

好友版安装器会在游戏目录放置 `cn_check_bypass.it`，内容为 `Better Multiplayer`，供支持该标记的中文补丁识别。先装好友版再装汉化时，在汉化安装器里选择游戏目录的 `isaac-ng.bettermultiplayer-original.exe`。下次启动好友版时会自动更新联机副本。

## 失步恢复

双方用同一版好友包启动。失步后，房主在原对局继续，客机确认游戏显示的加入提示，也可从 **Friend Match** 重新加入房主。客机回房后，房主清完当前房间即可同步房主的房间检查点，一起继续这一局。下楼时也会同步楼层检查点。

## Mod 同步

房主在建房前选好 Mod 和配置。客机需要同步时，会先看到游戏内弹窗，列出待下载的工坊 Mod。选择继续后，Steam 会准备所需 Mod，客机会重启并尝试回到原房。选择取消可留在游戏，离开房间后可重新尝试。两种房间都会对齐启用列表、Mod Config Menu 中文版的设置，以及 Mod 数据目录里的 config.json、settings.json 和 options.json。私人制作的本地 Mod 由各位玩家提前安装。

启动器和 Mod 同步组件无需读取或写入 `options.ini`。联机副本在游戏进程内选择 Steam 联机模式，同步重启时在本次游戏进程内采用房主的 Mod 总开关。游戏保存设置时会保留玩家原来的值。

启用角色 Mod 后，继续在好友房选角菜单向左或右切换即可找到自定义角色，普通和里角色沿用原菜单切换键。好友房沿用单人角色的肖像资源。开局前会核对双方加载的角色列表。

## GoodTrip

好友版内置 GoodTrip，无需订阅工坊项目。房主按住地图键，点击快传地图里的目标房间。也可用射击方向移动光标，再按确认或 Enter。标记 `@` 为当前房间，`o` 为可以前往的房间。

队伍可以在同层、已探索且清空的普通房间之间快传。好友版会跳过名称或目录中包含 `goodtrip` 或 `good trip` 的 Mod 脚本，这些 Mod 也不会被同步给客机。通过原版入口启动时仍按游戏的 Mod 设置加载。

## TimeMachine 和共享随机数

好友联机支持工坊 TimeMachine `2617557401` 的 1.7 脚本。机器加速用同一局、同一联机帧、同一房间和同一机器与玩家对应的随机值。各端应使用相同的 `tmmc_config.lua` 设置。

其他 Mod 可以在同步的游戏更新回调中使用 `BetterMultiplayerRng.Float(stream, entitySeed, actorSeed, sample)`。它返回 `[0, 1)` 的数，或在联机模拟外返回 `nil`。同一帧同一事件的键相同，结果也相同。`sample` 默认为 `0`，同一事件需要多次抽取时可以传不同的整数。

## 卸载

退出游戏，双击游戏目录中的 **BetterMultiplayer-Uninstall.exe** 并确认。Steam 恢复启动原版游戏。

联机记录位于 `%LOCALAPPDATA%\BetterMultiplayer\Friends-Mods` 下对应安装目录的 `Native-P2P\J514-Friends\transport.log` 或 `Native-P2P\J511-Friends\transport.log`。
