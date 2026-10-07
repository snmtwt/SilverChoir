# 验证记录

验证日期：2026-10-07（Asia/Shanghai）。引擎：本机 UE 5.8.3，Win64，VS 2022 / MSVC 14.44。

## 自动化结果

`CoverSmartObjects` 测试组共 **17 项通过，0 失败，0 未执行**。其中 15 项无警告；两项真实烘焙测试在销毁隔离世界时各有一条引擎 CrowdManager 找不到 RecastNavMesh 的警告，不影响测试断言。

| 测试 | 验证内容 |
| --- | --- |
| Debug.EditorConsolePreview | 编辑器无玩家时的全局显示开关、上限、世界坐标和探身线条 |
| Generation.NavBoundaryBake | 真实 Recast 烘焙；80cm低蹲、普通半墙、全墙两侧两端；固定墙角内缩50cm、墙角外20cm、总探身70cm；关闭低蹲不产生低蹲点 |
| Generation.QuantizedThinWallEnds | 重建 L_T5 薄墙坐标及35/144cm导航代理；两面×两端固定内缩、胶囊净空；更改采样间距仍固定50cm、参数改60cm后失效并重烘焙；过小内缩与不足探身预算安全拒绝 |
| Geometry.LowCrouchProfileAndAnchors | 低蹲轮廓、眼高、姿态输出及非法配置 |
| Geometry.PeekAndProfile | 坐标旋转、眼高、左右探头数据、非法胶囊配置 |
| Integration.AdjacentSlotsCannotOverlap | 不同原生槽位也不能预订相互重叠的身体位置 |
| Integration.EightyCentimeterLowCrouch | 80cm真墙、显式启用、72/62cm轮廓、保留120cm物理胶囊、高位敌人及顶障碍拒绝 |
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

完整报告：`Saved/CoverSmartObjectsValidation/CornerTestReport/index.json`。原始日志：`Saved/CoverSmartObjectsValidation/CornerAutomation.log`。低蹲版本及最初13项报告分别保留在 `LowCoverTestReport`、`TestReport`。

## 构建

- Win64 Development Editor 插件模块：UHT、C++ 编译、链接通过，输出 `Binaries/Win64/UnrealEditor-CoverSmartObjects.dll`。
- 非编辑器 Win64 Development 模块预编译：10 个编译动作通过；日志见 `Saved/CoverSmartObjectsValidation/GameBuild.log`。这不等同于完整游戏打包。

## 编辑器调试显示修复（2026-10-07）

- 修复 `cso.Debug 1` 原本只作用于运行时子系统、在普通编辑器世界不绘制的问题。Volume 在编辑器中直接预览已烘焙数据，不依赖玩家或运行时 Smart Object 注册；前景线条可穿过遮挡墙体显示。
- 新增 `CoverSmartObjects.Debug.EditorConsolePreview`，独立执行 **1 项通过、0 警告、0 失败**。验证无玩家的 Editor World 中全局开关 off/on/off、点数限制、本体开关独立性、世界坐标和三种探身方向的实际 LineBatch 输出。报告：`Saved/CoverSmartObjectsValidation/DebugTestReport/index.json`。
- 修复后的插件已完成 Development Editor 编译并在实际项目加载。L_T5 日志记录 12 个掩体 / 605 样本 / 247 边缘；打开 L_T5 并启用全局调试后，用户确认已看到掩体和箭头。本次窗口截图工具超时，因此视觉结果来自用户现场确认，没有截图验收记录。

## L_T5 单侧缺点与80cm墙（2026-10-07）

- 真实图诊断确认：Wall3 东侧导航边界在墙端只有约26cm物理间隙，固定后退8cm仍不足以容纳带净空的37cm半径；继续沿边到能站人的地方时已远离可探身墙角。西侧导航轮廓间隙约55cm，故此前只生成西侧点。法线方向实测正常，相反方向也不会被去重。
- 改为最多8轮、逐轮增加8cm、两法线交替的有限回退；每次重新做导航投射、落地、胶囊、身体遮挡和探身检查。独立薄墙测试的新旧算法对照：旧代码2点且断言失败，新代码4点全部满足净空。对照报告：`OldBoundaryTestReport`（预期失败），最终恢复版核验：`FinalBoundaryTestReport`。
- 80cm墙新增 LowCrouch 数据与查询支持，默认隐藏轮廓72cm、眼高62cm，保留真实蹲姿碰撞胶囊。查询须显式 `bAllowLowCrouch=true`；动画需匹配此轮廓，代码不会自动生成或修改角色动画。
- 只读加载真实 L_T5，在内存重新烘焙（未保存游戏地图）：原保存18点；通用配置修复后45点；匹配项目42cm半径、92cm站姿半高、60cm蹲姿半高后44点（19站姿、25低蹲），均来自735样本/257边缘。Wall2 恢复两面四端；Wall3 东南端恢复，东北内角因相邻斜墙阻挡探头继续拒绝。
- 地图检查报告：`L_T5ProjectProfile.json`；真实过滤日志：`L_T5Diagnostic.log`；场景验证日志：`L_T5FixedValidation.log`。场景命令行的退出码1来自项目已有 EasyHouseBuilderEditor 的 Toolset 注册错误，Python烘焙/检查本身完成且有明确成功日志。
- 用户保存并关闭编辑器后，已安装最终验证的插件 DLL；重新打开真实 L_T5，应用42/92/60cm物理尺寸及110cm普通蹲姿眼高，完成44点烘焙并保存地图，开启 `cso.Debug 1`。实际应用回执：`L_T5Applied.json`；日志 `Saved/Logs/SilverChoir.log` 的 `CSO_L_T5_APPLIED`。原地图与DLL备份位于 `Saved/CoverSmartObjectsValidation/Backups/20261007-025853`。

## 固定墙角内缩距离（2026-10-07）

- 新增 Volume 的 `CornerInsetDistance`，默认50cm；它是脚底中心沿墙切向到实际遮挡边缘的距离。以导航边界为发现种子，将碰撞边缘二分细化至0.125cm，再重定位到固定位置。碰撞轮廓包含端柱；重新落地、验证导航和身体净空时不能滑动切向坐标。侧探总位移缓存为 `CornerInsetDistance + LeanDistance`，默认70cm。
- 每侧独立定位；固定位置不可站立或不能隐蔽则拒绝。某一侧只能蹲姿使用时，单独检查该方向，避免另一侧站姿成功造成遗漏。低墙中段仍保留仅起身射击的点。生成版本2以及内缩参数快照使旧烘焙失效，修改距离后需要重新 Bake Cover。
- 完整17项自动化通过；最后的按侧姿态补点修正后，重新执行受影响的两个真实导航烘焙测试，均通过。最终报告 `CornerFinalGenerationTestReport/index.json`；最后编译日志 `CornerInsetFinalBuild.log`。
- 实际 L_T5 只读场景核验：分别烘焙内缩50cm、60cm、50cm，同时更改采样间距；每一轮全部24个侧探方向均通过独立的墙角内外±0.5cm碰撞射线校验，侧探缓存分别为70/80/70cm。Wall2两面四端、Wall3南端两面齐全；默认50cm时45点，其中25点低蹲。Wall2及Wall3解析边缘的最大偏差约0.067cm。报告 `L_T5CornerValidation.json`，日志 `CornerFinalSceneValidation.log`。
- 该变化增加编辑器烘焙阶段的角点定位，运行时继续读取缓存位置并验证当前目标，不增加运行时墙角搜索。
- 已备份原地图和插件二进制到 `Saved/CoverSmartObjectsValidation/Backups/20261007-031838-CornerInset`，安装最终 DLL 并校验哈希；真实编辑器重新打开 L_T5，按50cm内缩完成45点烘焙、保存地图并启用 `cso.Debug 1`。实际应用回执 `L_T5CornerApplied.json`；`Saved/Logs/SilverChoir.log` 中有 `CSO_CORNER_APPLIED` 成功记录。未声称截图视觉验收。

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

没有将“测试通过”当作商业化性能保证。尚需在实际地图和目标硬件验证：World Partition 加载时的大批实例注册耗时、100/500 AI 并发的 P95/P99 查询延迟、动态门/破坏的游戏事件接线、寻路与动画到达姿态、真实枪口射击、多人复制及完整 Shipping 打包。自动化使用 NullRHI；L_T5 的基础调试显示已由用户现场确认，运行时占用、标签、分区以及查询过程的全部视口表现仍需验收。

商业化下一步应优先完成：项目 AI/StateTree/HMS 的专用适配、按流送单元的分批注册和烘焙工具、NavMesh/破坏事件的局部增量重建，以及枪口和动画探身轨迹校验。当前版本是这些工作的独立可编译基础，并已实现所请求的核心检索和调试接口。
