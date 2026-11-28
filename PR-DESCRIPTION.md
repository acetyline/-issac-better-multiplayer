# PR: Pattern-based hook site location as a generic tier beyond the known builds

## 背景

v0.4.0 的 `BM_GAME_RVA` 双偏移很好地覆盖了 stock J511 与 J514（含中文补丁镜像）。但任何**再次改变 .text 布局**的本地补丁或后续游戏更新，仍需要为每个 hook 位点重新提取一整套偏移——目前 13 个位点分布在 6 个 C 文件里，逐个更新成本不低。

## 方案

新增 `transport/bm_locate.c/h`：一个一次性的带掩码模式扫描器，作为已知偏移之外的**通用第三层**：

- 13 个 hook 位点各配一条**唯一模式**（已在 stock J511 与 J514/汉化镜像上验证唯一命中；绝对地址与 call 位移全部通配，patch 后镜像会合法改变它们）。
- 5 个 hook 文件（friends / modsync / characters / goodtrip / recovery）在安装时**优先**使用定位结果，并从 call 指令自身推导原生函数目标；`BM_GAME_RVA` 偏移保留为回退——**两个已知构建的行为完全不变**。
- 模式定位的指针校验（IAT 槽、对话框处理函数）改为范围校验而非固定 RVA。
- `bm_locate_failure()` 返回失败位点索引，并入 lobby-cancel 日志，方便排障。
- `Start-NativeP2P.ps1` 保留上游 v0.4 版本原样（它已支持汉化镜像与 bootstp 处理，本 PR 不动它）。

## 验证

Windows Steam（汉化 J514 镜像 + MODSYNC 完整流程）实测：

- 模式唯一性：stock exe 13/13、汉化 exe 13/13、运行时内存（ASLR 重定位后）13/13
- 完整联机日志 9/9 全绿：Session options / Built-in GoodTrip / P2P adapter / Room item buffer guard / Modded pickup animation / character picker / Trinket speed / room recovery / checkpoint recovery
- modsync `error.txt` 无 setup 错误，游戏稳定运行
- stock J511 路径保留原逻辑（回退不变）

## 改动清单

- 新增 `transport/bm_locate.c`、`transport/bm_locate.h`
- 修改 `bm_friends.c`、`bm_characters.c`、`bm_goodtrip.c`、`bm_modsync.c`、`bm_recovery.c`（模式优先 + 推导目标）
- `Build.ps1`：源列表加入 `bm_locate.c`；`Add-Type` 补 `System.IO.Compression`（修复 Windows PowerShell 5.1 下 ZipArchive 类型缺失）

## 维护性说明

- 模式歧义（零/多命中）时拒绝安装并走回退，不盲猜。
- 未来游戏更新时：模式失效→自动回退到已知偏移（不劣化现状）；补齐新偏移时模式可同步重提取，也可只维护 `BM_GAME_RVA` 一条路——两套机制互不干扰。
