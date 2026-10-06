# 性能优化与回归测试记录

日期：2026-09-28。状态：代码修改、完整编译、本轮功能回归、主菜单及基地固定视角采样已完成；旧 H5 全量测试仍有 7 项失败，详见下文。

本轮针对纯 UI 主菜单的空世界渲染、H5 原生渲染临时内存与绑定同步、按钮稳定状态重复操作、区域名称控件重绘进行了优化。已确认主菜单每帧耗时减少；本报告不将该场景结果外推为战斗、基地或整个游戏的帧率提升。

## 测量条件与采样方法

| 项目 | 条件 |
| --- | --- |
| 引擎 | UE 5.8.3，构建号 58210709 |
| CPU | AMD Ryzen 9 5900X |
| GPU | NVIDIA GeForce RTX 4090 |
| 系统 / 图形接口 | Windows 10 22H2 / D3D12 |
| 主菜单分辨率 | 1920 × 1080 |
| 运行方式 | 独立 `-game -RenderOffscreen -Windowed -ForceRes` |
| 帧率与垂直同步 | `t.MaxFPS 0`、`r.VSync 0`，无帧率上限 |
| 单次 CSV 长度 | 1,800 帧 |
| 分析窗口 | 丢弃最后 10 帧，取此前末尾 900 帧；零起始区间 `[890, 1790)` |

两次采样均使用同一窗口规则，避开较早的启动阶段及末尾的采集关闭阶段。分析工具为 [summarize_performance_csv.py](S:/UE_WorkSpace/SilverChoir/Scripts/Editor/summarize_performance_csv.py:1)，参数为 `--frames 900 --skip-last 10`。当前是本机两次场景采样，尚不是多次重复试验的置信区间。

原始证据：

- [主菜单 Before 摘要](S:/UE_WorkSpace/SilverChoir/Saved/Profiling/Performance20260928/MainMenu_Before.summary.json) 与 [原始 CSV](S:/UE_WorkSpace/SilverChoir/Saved/Profiling/Performance20260928/MainMenu_Before.csv)。
- [主菜单 After 摘要](S:/UE_WorkSpace/SilverChoir/Saved/Profiling/Performance20260928/MainMenu_After.summary.json) 与 [原始 CSV](S:/UE_WorkSpace/SilverChoir/Saved/Profiling/Performance20260928/MainMenu_After.csv)。
- [Before 运行日志](S:/UE_WorkSpace/SilverChoir/Saved/Logs/Perf_MainMenu_Before.log)、[After 运行日志](S:/UE_WorkSpace/SilverChoir/Saved/Logs/Perf_MainMenu_After.log)。After 日志第 493 行包含实际采样命令。

## 已完成的四类代码修改

### 1. 纯 UI 主菜单跳过世界渲染

在本地玩家的菜单成功加入视口后，临时设置该 `UGameViewportClient` 的 `bDisableWorldRendering`。第一次取得管理权时记录原值；重复显示不会把临时值误记为原值。隐藏、移除、销毁、旅行和开始地图切换都会恢复原值。地图切换被拒绝且恢复菜单时，重新取得管理权。

蓝图直接调用 `SetVisibility(Hidden/Collapsed)` 同样释放该状态；重新显示时重新读取当时的视口原值。外部 `RemoveFromParent` 会立即通知控制器，即使还保留着 Slate 引用、`NativeDestruct` 尚未发生，也不会等到析构才恢复世界渲染。

具体位置：

- [MainMenuPlayerController.h:25](S:/UE_WorkSpace/SilverChoir/Source/SilverChoir/Public/Map/MainMenu/MainMenuPlayerController.h:25)：新增 `bRenderWorldBehindMenu`，默认 false；未来使用 3D 菜单背景时设为 true。
- [MainMenuPlayerController.cpp:22](S:/UE_WorkSpace/SilverChoir/Source/SilverChoir/Private/Map/MainMenu/MainMenuPlayerController.cpp:22)：成功显示与失败恢复。
- [MainMenuPlayerController.cpp:82](S:/UE_WorkSpace/SilverChoir/Source/SilverChoir/Private/Map/MainMenu/MainMenuPlayerController.cpp:82)：取得视口状态及保存原值；101 行统一恢复。
- [MainMenuPlayerController.cpp:138](S:/UE_WorkSpace/SilverChoir/Source/SilverChoir/Private/Map/MainMenu/MainMenuPlayerController.cpp:138)：切换地图前释放；拒绝分支恢复菜单优化。
- [MainMenuWidget.cpp:54](S:/UE_WorkSpace/SilverChoir/Source/SilverChoir/Private/Map/MainMenu/MainMenuWidget.cpp:54)：显隐、外部移除及析构通知。
- [MenuTravelTests.cpp:61](S:/UE_WorkSpace/SilverChoir/Source/SilverChoir/Private/Tests/MenuTravelTests.cpp:61)：作用域测试；实际进入基地的测试还检查世界渲染是否恢复。

此方案保持菜单的 H5/UMG 绘制，不通过修改画质或限制帧率获得收益。当前按项目单本地玩家设计；未来多个控制器同时管理同一视口时，应改为统一管理所有权。

### 2. H5 原生 Slate 提交与数据绑定减少重复工作

- [H5UI_Interfaces.h:119](S:/UE_WorkSpace/SilverChoir/Plugins/H5UIPlugin/Source/H5UIPlugin/Private/H5UI_Interfaces.h:119) 保存可复用的顶点暂存数组；[H5UI_Interfaces.cpp:968](S:/UE_WorkSpace/SilverChoir/Plugins/H5UIPlugin/Source/H5UIPlugin/Private/H5UI_Interfaces.cpp:968) 管理暂存借用与递归提交回退，减少同等几何规模下反复申请临时数组。Slate 提交保留各次顶点副本，不共享后续会被覆盖的提交数据。
- [H5UI_RuntimeView.cpp:1833](S:/UE_WorkSpace/SilverChoir/Plugins/H5UIPlugin/Source/H5UIPlugin/Private/H5UI_RuntimeView.cpp:1833) 比较实际 DOM 内容、属性和表单值，仅有变化才写入。空白文本按 Rml 的实际表示规范化；不只比较上一次模型值，以免忽略 JavaScript 对 DOM 的修改。
- [H5UI_RuntimeView.cpp:1907](S:/UE_WorkSpace/SilverChoir/Plugins/H5UIPlugin/Source/H5UIPlugin/Private/H5UI_RuntimeView.cpp:1907) 周期同步时发现当前树中的绑定元素，避免对无绑定元素重复做模型同步；仍遍历当前 DOM，从而识别运行时新增或重命名的绑定。
- [H5UI_RuntimeView.cpp:1959](S:/UE_WorkSpace/SilverChoir/Plugins/H5UIPlugin/Source/H5UIPlugin/Private/H5UI_RuntimeView.cpp:1959) 按 0.25 秒发布周期处理性能模型。只有 JavaScript 可能读取，或实际绑定引用 `perf.*` 时才生成对应统计字符串。
- 新增 CSV 计时/计数及 [绑定回归测试](S:/UE_WorkSpace/SilverChoir/Plugins/H5UIPlugin/Source/H5UIPlugin/Private/Tests/H5UI_BindingPerformanceTests.cpp:8)、[顶点暂存回归测试](S:/UE_WorkSpace/SilverChoir/Plugins/H5UIPlugin/Source/H5UIPlugin/Private/Tests/H5UI_RenderScratchTests.cpp:11)。

CSS 动画仍正常推进；没有为了减少绑定成本而停掉主菜单动画、切换到 CEF 或更改 H5 目标帧率。

### 3. 原生按钮稳定状态避免重复写入和捕获查询

- [BasicButtonWidget.cpp:166](S:/UE_WorkSpace/SilverChoir/Source/SilverChoir/Private/UIBasic/BasicButtonWidget.cpp:166)：没有待处理按压且没有残留 `ActiveKey` 时，取消操作直接返回，避免禁用按钮每帧重复查询 Slate 鼠标捕获。
- [BasicButtonWidget.cpp:206](S:/UE_WorkSpace/SilverChoir/Source/SilverChoir/Private/UIBasic/BasicButtonWidget.cpp:206)：继续检查父级显示/启用状态，继续推进两帧按压及悬浮插值；只有颜色与控件当前值不同才调用颜色 setter。
- 比较的是控件实时颜色，不是仅缓存目标颜色，因此蓝图同步或直接修改子控件样式后仍能正确刷新。
- [BasicButtonWidgetTests.cpp:134](S:/UE_WorkSpace/SilverChoir/Source/SilverChoir/Private/Tests/BasicButtonWidgetTests.cpp:134)：新增 500 按钮稳定状态微基准；原输入测试追加父级显隐、禁用和样式变更验证。

没有关闭按钮 Tick，也没有改变点击声音、两帧反馈、悬浮、边框或选中效果。本轮未重构人员整备室名单，以免把列表生命周期修改混入平均帧耗时优化。

### 4. 区域名称控件按需重绘，保留蓝图动画兼容

- [RegionBlock.h:44](S:/UE_WorkSpace/SilverChoir/Source/SilverChoir/Public/Map/BaseMap/RegionBlock.h:44)：名称持续更新开关。
- [RegionBlock.cpp:96](S:/UE_WorkSpace/SilverChoir/Source/SilverChoir/Private/Map/BaseMap/RegionBlock.cpp:96)：仅当 `NameWidgetClass == URegionNameWidget::StaticClass()`，且没有开启持续更新时，启用手动重绘并关闭连续 Tick。这里是精确类比较，**不包含任何 WBP 子类**。
- [RegionBlock.cpp:133](S:/UE_WorkSpace/SilverChoir/Source/SilverChoir/Private/Map/BaseMap/RegionBlock.cpp:133)：显示时请求一次更新；隐藏时停止组件 Tick。
- [RegionBlock.cpp:167](S:/UE_WorkSpace/SilverChoir/Source/SilverChoir/Private/Map/BaseMap/RegionBlock.cpp:167)：名称、悬浮、点击脉冲等状态变化后显式请求渲染更新。

WBP 名称控件继续连续更新，保留用户蓝图中可能存在的动画和动态内容。因此不能把纯原生名称控件的收益直接套用到使用 WBP 的整个基地。

## 主菜单性能结果

单位均为毫秒，来自上述 900 帧分析窗口。

| 指标 | Before 平均 | After 平均 | Before P95 | After P95 |
| --- | ---: | ---: | ---: | ---: |
| FrameTime | 4.34855 | 1.17718 | 5.2024 | 1.4566 |
| GameThreadTime | 2.14699 | 1.17524 | 2.7301 | 1.5026 |
| RenderThreadTime | 4.34712 | 0.81320 | 5.2096 | 1.0570 |
| RHIThreadTime | 2.08661 | 0.49369 | 2.4951 | 0.6390 |
| GPUTime | 1.92588 | 0.17329 | 2.0167 | 0.1936 |
| Slate DrawPrePass | 0.07164 | 0.04010 | 0.1066 | 0.0616 |
| Slate SObjectWidget Tick | 0.01928 | 0.00651 | 0.0285 | 0.0091 |
| GPU SlateUI | 0.07833 | 0.07833 | 0.0909 | 0.0852 |

该次主菜单平均帧耗时减少约 **3.171 ms**，GPU 帧耗时减少约 **1.753 ms**。`1000 / 平均 FrameTime` 换算值为 **229.96 → 849.49 FPS**；这是该离屏、无上限主菜单场景的换算数值，不是所有游戏场景的性能承诺，也不是逐帧 FPS 的算术平均值。

Before 的空世界仍执行了约 0.39575 ms 的 TSR、0.27603 ms 的延迟光照、0.21089 ms 的后处理；After 摘要不再出现这些世界渲染分项。与此同时 GPU SlateUI 平均耗时仍为 0.07833 ms，符合“保留 UI，跳过其后方空世界”的优化方向。多个优化共同参与了 After 采样，当前没有逐项开关对照，不能把总差值全部归因到某个小型 C++ 改动。

CPU、渲染线程、RHI 与 GPU 存在并行和等待关系，表中各列不能直接相加为总帧耗时。CSV 记录的是每帧耗时，**没有测得 CPU/GPU 使用率百分比下降**。在无帧率上限下，处理器可能将节省的时间用于渲染更多帧，任务管理器使用率仍可能较高。

## 构建和功能回归

| 检查 | 结果 | 证据 |
| --- | --- | --- |
| 完整 C++ 构建 | 成功，`Result: Succeeded` | [PerformanceOptimizationBuild.log](S:/UE_WorkSpace/SilverChoir/Saved/Logs/PerformanceOptimizationBuild.log) |
| 编辑器自动化集合 | **34 / 41 通过，7 项失败** | [Perf_EditorRegression.log](S:/UE_WorkSpace/SilverChoir/Saved/Logs/Perf_EditorRegression.log) |
| 本轮新增 H5 回归 | 3 / 3 通过 | 同上：`ScratchReuseAndSubmissionCopies`、`BindingSynchronizationPerformance`、`DynamicPerformanceBindings` |
| UI/按钮回归 | 6 / 6 通过 | 同上：背景动画、500 按钮、输入与内容、两帧延迟、菜单退场顺序、像素边框 |
| 战略移动、车辆、小队和共享单位数据 | 本轮选择的相关测试通过 | 同上，详见 `Test Completed` 条目 |
| 基地运行时回归 | 16 / 16 通过 | [Perf_RuntimeRegression.log](S:/UE_WorkSpace/SilverChoir/Saved/Logs/Perf_RuntimeRegression.log) |
| 主菜单世界渲染作用域 | 1 / 1 通过 | [Perf_MenuScope.log](S:/UE_WorkSpace/SilverChoir/Saved/Logs/Perf_MenuScope.log) |
| 人员整备室专项回归 | 1 / 1 通过 | [Perf_PersonnelRegression.log](S:/UE_WorkSpace/SilverChoir/Saved/Logs/Perf_PersonnelRegression.log) |

基地 16 项覆盖人员整备室进入/退出、预加载 UI、提前切换装备、仓库返回；小队会议室动画、布局、数据控件、中断、草稿保存导航、容量；场景生命周期及区域几何、点击、运行交互。该组成功不代表所有插件、地图和游戏业务都已覆盖。

菜单作用域检查了显示/隐藏/重开、直接 Hidden/Collapsed 显隐、保存原值 true、外部移除、3D 菜单选项。完整菜单到基地旅行的 `SilverChoir.MenuTravel.StartAction` 也已通过，包含世界渲染恢复、加载进度与延迟关闭断言，见 [Perf_BaseFixedOrigin_After.log](S:/UE_WorkSpace/SilverChoir/Saved/Logs/Perf_BaseFixedOrigin_After.log)。以上共有 53 个不同测试通过，7 个旧 H5 测试失败，不重复计数重复执行的旅行测试。

车辆资产另作只读复验：3 个表行、3 张约 16:9 图片及 3 次模板工厂创建均通过，见 [Perf_VehicleAssetsVerify.log](S:/UE_WorkSpace/SilverChoir/Saved/Logs/Perf_VehicleAssetsVerify.log) 的 `VEHICLE_TEMPLATES_OK rows=3 textures=3 factory=3 verify_only=True`。该命令进程仍因启动阶段已有插件错误返回 1；这里依据脚本完成标记及 [JSON 验证记录](S:/UE_WorkSpace/SilverChoir/Saved/VehicleTemplateImports/20260927T190920132747Z.json) 确认资产验证成功，并非声称整个启动日志无错误。

### 500 按钮微基准

500 个原生按钮，其中 250 个由父控件禁用；预热后同步调用 300 轮 NativeTick，共 150,000 次。日志记录：

```text
optimized_ms=32.195
ms_per_500_ticks=0.107318
color_writes=0
capture_queries=0
source_baseline_color_writes=300000
source_baseline_capture_queries=75000
```

32.195 ms 是该次微基准实测 CPU 时间；每轮 500 个按钮平均 0.107318 ms。旧版本 300,000 次颜色写入及 75,000 次捕获查询是根据原代码每 Tick 无条件调用推导的计数，**没有旧版本微基准耗时作为速度比对**。此测试不绘制完整游戏画面，也不测实际 GPU 或整机 FPS。

### 未通过的 7 个已有 H5 测试

| 测试 | 日志表现 / 当前处理状态 |
| --- | --- |
| `H5UIPlugin.Runtime.BaseControlInventoryLayout` | 旧 BaseControl 演示页加载失败，后续布局断言随之失败。 |
| `H5UIPlugin.Runtime.BrowserStyleLayoutParity` | 依赖旧 CommanderOS 等演示页面和其布局资源，当前测试资源根不完整。 |
| `H5UIPlugin.Runtime.RepeatedPointerSessions` | 依赖旧 BaseControl 页面；加载失败后拖拽、详情等交互无法建立。 |
| `H5UIPlugin.Runtime.StrategyControlCompatibility` | 旧 StrategyControl 页面/关联资源无法完整加载，例如 `routes/route-h.svg`；兼容断言失败。 |
| `H5UIPlugin.Runtime.TacticalMapStableUpdate` | `coui://uiresources/TacticalMap/V5/tactical-map.html` 演示夹具不可用，后续布局/绘制断言失败。 |
| `H5UIPlugin.Runtime.ViewportResizeEvent` | 旧断言期望的 resize 语义与当前缩放处理不一致：期望 `800|600|1`，实际 `800|600|2`；后续期望计数 1，实际 3。 |
| `H5UIPlugin.Runtime.NativeSmoke` | 多类失败，包括颜色输入几何、高 DPI/布局断言，以及旧 MainMenu/BaseControl 演示资源缺失所引出的断言；不能归并为单一原因。 |

部分旧演示文件仍在插件 WebSDK 示例目录中，但这不等于测试 URL 对应的当前项目资源根已经具备全部文件。上述失败需要补齐夹具、复核旧语义和独立定位；本轮没有将失败用例跳过或改为无条件通过。尚未逐项用修改前版本重跑这些失败，因此也不能声称所有失败都与本轮修改无关。**当前不是全测试通过状态。**

## 基地固定视角性能

`BaseAnchored_After` 已判定无效并排除：相机 Y 坐标漂移至约 -92603，截图为黑色。采样用的 `K2_SetActorLocation` exec 缺少必要参数，虽然日志显示调用成功，实际上没有执行预期定位；不能拿黑画面的低耗时作为优化结果。

有效对比为 `BaseAnchored_Before` 与 `BaseFixedOrigin_After`。两次稳定窗口内视点均为 `(-2500, 0, 4330.127)`，已人工核对 [Before 截图](S:/UE_WorkSpace/SilverChoir/Saved/Profiling/Performance20260928/BaseAnchored_Before.png) 与 [After 截图](S:/UE_WorkSpace/SilverChoir/Saved/Profiling/Performance20260928/BaseFixedOrigin_After.png) 的基地结构、视角及文字位置一致。截图仅用于核对 3D 场景，HighResShot 不含屏幕 UI；基准运行期间基地 UI 正常存在。

| 指标 / ms | Before 平均 | After 平均 | Before P95 | After P95 |
| --- | ---: | ---: | ---: | ---: |
| FrameTime | 7.74776 | 6.91415 | 9.05 | 7.63 |
| GameThreadTime | 2.24861 | 1.93872 | 2.89 | 2.40 |
| GPUTime | 3.60218 | 3.53027 | 3.98 | 3.79 |

按平均帧耗时换算为 **129.07 → 144.63 FPS**。这是一次固定视角对比，GPU 差异约 0.07 ms，不能认为基地 GPU 成本已显著下降；CPU 游戏线程差异约 0.31 ms。缓存、运行时调度等也会影响结果，不能把全部差值归因于本轮代码。未使用异常偏小的基地 RenderThreadTime 统计作收益结论。

两次均采集 6,000 帧，分析 `[5090, 5990)` 的 900 帧，原始数据和精确 P95 见 [Before 摘要](S:/UE_WorkSpace/SilverChoir/Saved/Profiling/Performance20260928/BaseAnchored_Before.summary.json) 与 [After 摘要](S:/UE_WorkSpace/SilverChoir/Saved/Profiling/Performance20260928/BaseFixedOrigin_After.summary.json)。After 启动阶段临时限制 120 FPS，以保证第 3800 帧定位相机时旅行已完成；第 3801 帧恢复 `t.MaxFPS 0`，整个分析窗口均无帧率上限。未更改项目持久帧率设置。Before 的定位命令缺参但实际稳定视点恰好已在原点；只有经过 CSV 坐标和截图核实的该次 Before 被保留用于对比。

`Base_Before_Initial`、`Base_Before`、`BaseFixed_Before` 和 `BaseAnchored_After` 存在相机漂移或黑画面，均排除。它们保留在 Saved 下便于审计，不能用于宣传性能收益。

完整有效 After 采样命令见 [运行日志](S:/UE_WorkSpace/SilverChoir/Saved/Logs/Perf_BaseFixedOrigin_After.log)。相机固定脚本的正确命令为：

```text
ke BP_PlayerPawn_C_0 SetCameraMovementDisabled true
ke BP_PlayerPawn_C_0 SetCameraRotationDisabled true
ke BP_PlayerPawn_C_0 CancelCameraStateMove
ke BP_PlayerPawn_C_0 K2_SetActorLocation (X=0,Y=0,Z=0) false () true
```

原生 UE `exec` 使用 Engine/Binaries 下的相对文件名；本次临时复制的脚本在测试完成后删除，源码保留在 [FreezeBaseCamera.txt](S:/UE_WorkSpace/SilverChoir/Saved/Profiling/Performance20260928/FreezeBaseCamera.txt)。这只是独立采样进程中的操作，不会保存或禁用用户正常游戏的相机输入。

## 画面检查与后续性能方向

优化后的 [主菜单第 1 帧截图](S:/UE_WorkSpace/SilverChoir/Saved/Screenshots/MenuQA/PerfAfter_1.png) 与 [第 2 帧截图](S:/UE_WorkSpace/SilverChoir/Saved/Screenshots/MenuQA/PerfAfter_2.png) 相隔约 2 秒：雷达扫描方向、环线和标题扫光均发生变化，按钮及布局保留。对应日志为 [Perf_MenuVisual.log](S:/UE_WorkSpace/SilverChoir/Saved/Logs/Perf_MenuVisual.log)。

本轮没有降低材质、阴影、分辨率或模型质量，没有删除 CEF，也没有修改库存插件的截图逻辑。下一阶段若继续优化基地 GPU，应在实际目标硬件和打包版本中，针对固定路线检查光照、阴影、透明叠加和物品捕获成本。人员列表批量更新与隐藏库存控件生命周期也值得独立测量；尚无证据证明它们是当前基地平均帧耗时的主要瓶颈，因此没有在本轮直接改写业务逻辑。

复验测试模式：`SilverChoir.MenuTravel.WorldRenderingScope` 与 `SilverChoir.MenuTravel.StartAction` 各自从主菜单启动独立 `-game` 进程；不要用共同前缀在同一进程全跑，因为 StartAction 会旅行到基地。按钮与 H5 原生测试使用编辑器自动化模式，不能用 `-game` 选择仅标记 EditorContext 的测试。
