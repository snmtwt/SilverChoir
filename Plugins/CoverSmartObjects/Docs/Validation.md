# 验证记录

验证日期：2026-10-07（Asia/Shanghai）。引擎：本机 UE 5.8.3，Win64，VS 2022 / MSVC 14.44。

## 自动化结果

`CoverSmartObjects` 测试组共 **13 项通过，0 失败，0 未执行**。其中 12 项无警告；真实烘焙测试在销毁隔离世界时有一条引擎 CrowdManager 找不到 RecastNavMesh 的警告，不影响测试断言。

| 测试 | 验证内容 |
| --- | --- |
| Generation.NavBoundaryBake | 临时 Editor World 中真实建立 Recast NavMesh 并执行 BakeCover；14 点 / 112 样本 / 20 边缘；验证半墙与墙角外 20cm |
| Geometry.PeekAndProfile | 坐标旋转、眼高、左右探头数据、非法胶囊配置 |
| Integration.AdjacentSlotsCannotOverlap | 不同原生槽位也不能预订相互重叠的身体位置 |
| Integration.LowWallAndDynamicOcclusion | 低墙起身、敌人去重、后方敌人软惩罚、墙变化/移除、预算用尽 |
| Integration.MovementOnlyCeilingBlocksStanding | 天花板忽略 Visibility 但阻挡 Pawn 时仍不能站起 |
| Integration.MovementOnlyObstacleBlocksLateralHead | 侧向物理障碍独立阻止探头 |
| Integration.PawnsAreNeitherCoverNorSightObstacles | 敌人胶囊不是掩体；默认和自定义对象通道 Pawn 的命中重试受预算限制 |
| Integration.ReservationAndInvalidation | 原生 Claim / Occupy / Renew / Release、竞争和失效 |
| Integration.StaleLeaseDoesNotReleaseExternalOwner | 外部释放并重新 Claim 后，旧租约不能操作新持有者 |
| Integration.TallWallLateralTwentyCentimeters | 真实碰撞墙角与实际烘焙距离、身体侧面露出时拒绝 |
| Partition.NegativeAndBoundaryCoordinates | 负坐标、格子边界、不同楼层 |
| Query.RankingTradeoffs | 距离/暴露惩罚、优先安全、确定性排序 |
| Query.WorkLimitsAndSpatialOrder | 输入上限、非法参数、分区由近及远的遍历顺序 |

临时 Host 位于 `Saved/CoverSmartObjectsValidation/Host`，只关联新插件。测试没有修改或保存 SilverChoir 的游戏地图。真实烘焙测试创建碰撞地板、半墙和全墙，不使用预填掩体点；其他集成测试在隔离 Game World 中创建实际物理形状及真正的 Smart Object 实例。

报告：`Saved/CoverSmartObjectsValidation/TestReport/index.json`。原始日志：`Saved/CoverSmartObjectsValidation/Automation.log`。

## 构建

- Win64 Development Editor 插件模块：UHT、C++ 编译、链接通过，输出 `Binaries/Win64/UnrealEditor-CoverSmartObjects.dll`。
- 非编辑器 Win64 Development 模块预编译：10 个编译动作通过；日志见 `Saved/CoverSmartObjectsValidation/GameBuild.log`。这不等同于完整游戏打包。

可复现的编辑器测试命令（PowerShell）：

```powershell
& 'R:\EpicGame\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'S:\UE_WorkSpace\SilverChoir\Saved\CoverSmartObjectsValidation\Host\CoverSmartObjectsHost.uproject' `
  -unattended -nop4 -nosplash -nosound -NullRHI `
  '-ExecCmds=Automation RunTests CoverSmartObjects' `
  '-TestExit=Automation Test Queue Empty' `
  '-ReportExportPath=S:\UE_WorkSpace\SilverChoir\Saved\CoverSmartObjectsValidation\TestReport'
```

注意查看 JSON 报告的 `failed` 字段；UE 自动退出进程的退出码不一定反映测试失败。

## 尚未完成的产品验收

没有将“测试通过”当作商业化性能保证。尚需在实际地图和目标硬件验证：World Partition 加载时的大批实例注册耗时、100/500 AI 并发的 P95/P99 查询延迟、动态门/破坏的游戏事件接线、寻路与动画到达姿态、真实枪口射击、多人复制及完整 Shipping 打包。当前自动化使用 NullRHI，因此没有完成调试图形的人工视口验收。

商业化下一步应优先完成：项目 AI/StateTree/HMS 的专用适配、按流送单元的分批注册和烘焙工具、NavMesh/破坏事件的局部增量重建，以及枪口和动画探身轨迹校验。当前版本是这些工作的独立可编译基础，并已实现所请求的核心检索和调试接口。
