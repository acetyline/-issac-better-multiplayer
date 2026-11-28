# PR: Resolve hook sites by unique byte patterns, not fixed RVAs

## 问题描述

BM 的所有游戏内 hook（lobby cancel、checkpoint recovery、character picker、trinket sync、GoodTrip、modsync start gate）都用**硬编码 J511 RVA** 定位游戏代码。当玩家在游戏 exe 上应用了**修改代码的本地补丁**（社区中文汉化 patcher，它注入 loader 并整体平移 .text 段），所有硬编码地址失效，hook 安装静默失败——联机的基础 P2P 能跑，但防失步恢复、好友房取消保护、联机选人、Mod 同步启动门等核心功能全部缺失。

## 方案

新增 `transport/bm_locate.c/h`——一次性模式扫描器：

- 13 个 hook 位点改由**掩码字节模式**在运行中的 .text 段定位。模式提取自 stock J511，并在【stock 镜像】与【汉化后镜像】上分别验证**唯一命中**。绝对地址和 call 位移一律通配（patch 后镜像会合法地改变它们）。
- 模式查找成功后，原生函数目标（如 `NATIVE_SEND_COMPLETE`）从 call 指令自身推导，而非比对固定 RVA。
- **历史偏移保留为回退**：模式找不到时按老逻辑走，stock 镜像行为与现在完全一致。
- `Start-NativeP2P.ps1` 同步升级：
  - 接受本地补丁过的游戏镜像（如 `isaac-ng.cn-pure.exe`，按"非 stock 时间戳"识别），构建联机副本时保留汉化；
  - 6 处指令补丁（modsPrompt/restartPrompt/steammode/crossplay/steampath/eosinvite）改为模式定位，不再依赖固定 RVA；
  - stock 校验失败时退而校验 PE 结构（PE32/x86），不再仅按字节数硬拒。

## 安全性考虑

- 模式歧义（多命中/零命中）时**拒绝安装**并返回 false，不会盲猜地址。
- 每处 hook 在写入前仍做原有的字节形态校验（E8/push opcode、rel32 自洽性、目标推导一致性）。
- `bm_locate_failure()` 返回失败位点索引，已并入 lobby-cancel 的日志输出，方便用户报告问题。

## 验证

Windows Steam J511 实测（含中文汉化共存环境）：

- 模式唯一性：stock exe 13/13、汉化 exe 13/13、游戏运行时内存 dump（ASLR 后）13/13
- 游戏正常启动，标题含 `(better multiplayer)`
- transport 日志：`Steam P2P adapter ready` + `Host checkpoint recovery ready`，无 hook 失败
- stock 镜像 + 本补丁同样通过编译与构建（Build.ps1 / Build-FriendsSetup.ps1）

## 改动清单

- 新增 `transport/bm_locate.c`、`transport/bm_locate.h`
- 修改 `transport/bm_friends.c`、`bm_characters.c`、`bm_goodtrip.c`、`bm_modsync.c`、`bm_recovery.c`、`bm_transport.c`、`Start-NativeP2P.ps1`、`Build.ps1`
- `Build.ps1`：编译列表加入 bm_locate.c；`Add-Type` 补 `System.IO.Compression`（Windows PowerShell 5.1 下 ZipArchive 类型缺失导致打包失败）

## 备注

- 模式覆盖的是 J511 布局；游戏更新（J512+）时模式与历史偏移都要重新提取，这与现状的维护成本一致。
- 有意保持零新依赖：纯 C99，仅 Windows API。
