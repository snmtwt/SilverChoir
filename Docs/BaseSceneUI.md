# 基地总 UI 与场景子 UI

基地总界面：`/Game/System/Map/BaseMap/UI/BP_BaseMapWidget`。
顶部、底部与中间内容容器均在这个蓝图的 Designer 中编辑。原有事件图保留。
货币为设计文档定义的瓦尔（Val）；运行时默认金额为 0，不代表已经实现钱包系统。

## 场景 UI

C++ 位于 `Source/SilverChoir/Public/Map/BaseMap/SceneUI`：

| C++ 类 | 子蓝图 | 场景 Tag |
| --- | --- | --- |
| USquadMeetingRoomWidget | WBP_SquadMeetingRoom | GameScene.SquadMeetingRoom |
| UCommanderOfficeWidget | WBP_CommanderOffice | GameScene.CommanderOffice |
| UOperationsCommandRoomWidget | WBP_OperationsCommandRoom | GameScene.OperationsCommandRoom |
| UPersonnelPreparationRoomWidget | WBP_PersonnelPreparationRoom | GameScene.PersonnelPreparationRoom |

这四个控件继承 UBaseSceneWidget，C++ 按场景放在 SceneUI 下的
SquadMeetingRoom、CommanderOffice、OperationsCommandRoom、PersonnelPreparationRoom 子目录中。
子蓝图也位于 `/Game/System/Map/BaseMap/UI/SceneUI/<场景名>`。
人员整备室已制作列表和三个详情页面，小队会议室提供小队管理界面，作战指挥室提供右侧时间控制与瓦片信息容器；团长办公室仍提供空白 ContentRoot。
公共数据：SceneTag、SceneTitle、BaseUI。
事件：OnSceneUIOpened、OnSceneUIClosed；用于订阅、刷新、解除订阅等。

## 蓝图使用

BaseWidget 的类默认值中配置 SceneUIClasses：Key 为精确匹配的 GameplayTag，
Value 为 UBaseSceneWidget 子蓝图类，已填入四个房间的默认配置。
调用 CreateSceneUIByTag 创建并挂载，GetCurrentSceneUI 获取当前实例，
UnloadCurrentSceneUI 请求卸载并等待完成通知。无效 Tag/类不会移除当前 UI，同一 Tag/类重复调用会复用当前实例。

子蓝图重载 LoadSceneUI / UnloadSceneUI 完成初始化和清理。
加载流程：挂载 → LoadSceneUI → 蓝图播放入场动画 → 动画 Finished → NotifyLoadCompleted（通知加载完成）→ OnLoadCompleted。
卸载流程：UnloadSceneUI → 蓝图播放退场动画 → 动画 Finished → NotifyUnloadCompleted（通知卸载完成）→ OnUnloadCompleted → RemoveFromParent → 清空 BaseUI/当前引用。
完成事件仍是可绑定的事件分发器；手动触发请调用对应 Notify 节点，不直接广播分发器。
没有动画时也需要在加载/卸载事件末尾调用对应 Notify 节点，否则不会完成。
切换时创建函数返回待进入的新实例，旧实例在卸载完成前仍是 CurrentSceneUI，新实例尚未挂载且 BaseUI 为空。
旧实例卸载完成后自动挂载新实例并调用其加载事件。切换中重复请求相同目标返回待进入实例，其他目标返回空。
UnloadCurrentSceneUI 会取消待进入实例并继续等待当前退场；卸载中的晚到加载通知、重复完成通知均忽略。
基地总 UI 自身移除时强制释放所有子 UI，不等待动画，也不伪造完成通知。
移除后实例由 UE 垃圾回收释放；外部持有的引用不会被强制销毁。
默认加载/卸载实现仍调用旧 OnSceneUIOpened / OnSceneUIClosed，兼容已有蓝图；
覆盖新函数时，如需保留旧事件调用，请调用父实现。

- 从 GameMainMapPlayerController 取得 BaseWidget，调用 ShowSceneUI，传入对应控件蓝图类。
- ShowSceneUI 仅管理界面：关闭并移除旧控件、创建新控件、更新标题、触发打开事件。重复传入当前类会复用实例。
- 场景管理插件的 EnterScene/LeaveScene 仍负责实际场景逻辑。在自己的场景蓝图中调用 ShowSceneUI / ClearSceneUI；不会自动改动已有场景事件图。
- 调用 SetCurrencyAmount 更新金额（int64，负数按 0 显示）；SetHeaderTitle 更新标题；SetFooterStatus 更新底部状态。
- 返回按钮广播 OnBackRequested，在总 UI 蓝图中绑定实际返回逻辑。没有硬编码返回某个场景或地图。
- ModalLayer 保持在最上层，中间空白区域不会拦截基地地图交互。

通用小按钮：`/Game/System/UIBasic/WBP_ShellButton`，继承现有 UBasicButtonWidget，
保留悬浮音效、按下即播放点击声音、两帧后反馈的行为。
瓦尔图标：`/Game/System/UIBasic/Textures/T_ValCurrency`，原图保存在 SourceAssets/UI。

布局使用 SafeZone 与左右边缘锚点，顶部与底部全宽，中间留作场景内容，不按 16:9 裁切。
未添加人员档案、装备配置、技能训练导航。

货币数字在名称下方左对齐，位数增加时向右延伸。图标启用 SimpleAverage MipMap、
Trilinear 采样与 NeverStream，避免 1024px 原图缩到小尺寸时直接采样产生锯齿。

HeaderEffects、FooterEffects 是 H5UI 原生 Slate 装饰层，分别加载
`Content/UI/BaseShell/header.html`、`footer.html`，共用 `shell.css`。
只绘制顶部与底部，30 FPS，不接收输入，不启用 JavaScript 或 CEF。
文字仍是 UMG TextBlock，使用 `/Game/System/UIBasic/Materials` 下的
M_TextSheen 与两个 MI_Base…Sheen 材质实例。Gain 控制扫光亮度，
Interval 控制间隔，Duration 控制扫光持续时间，保持原有字体颜色。

维护工具：BaseUIAssets commandlet 只在缺少 BaseShellSafeZone 时创建总界面布局；
不会覆盖后续 Designer 修改。保存前备份现有资产至 Saved/BaseUIBackups。
验证：SilverChoir.BaseUI.Shell，覆盖四个房间的实例切换、旧控件释放与金额刷新，并输出运行截图。

## 人员整备室

入口蓝图：
`/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/WBP_PersonnelPreparationRoom`。
左右栏、人员详情 WidgetSwitcher 与随身背包网格均在 Designer 中编辑。
左右面板等宽，各占可用区域的四分之一，中间 ModelDisplayArea 为透明的人员模型预留区。
详情页支持滚动，背包固定在右侧下方。
列表头像为 68×68，档案头像为 112×112；PersonnelViewData.Portrait 可设置真实头像纹理。
未设置纹理时显示轮廓占位图。头像与列表项组件蓝图放在人员整备室 Components 目录中。
左侧筛选只有“待命”和“全部”，右侧页签为“人员档案”“装备配置”“技能训练”。
切换页签保留选中人员；筛选移除当前人员时自动选中首个符合条件的人员。
装备配置页签把左侧切换为 WarehousePanel（当前仅空背景）；人员档案和技能训练恢复 RosterPanel。
CurrentListDisplay（Personnel / Warehouse）记录当前左侧展示状态，切换不会清空人员选择和筛选。
页签切换采用顺序动画：旧列表滑出完成后才切换可见性与 CurrentListDisplay，然后播放新列表滑入。
bListTransitioning 表示切换进行中。连续点击保留最后一次目标，当前滑入结束后再处理新的切换；同类人员页签之间不重复播放。
界面关闭或销毁会解除切换回调，取消尚未进入的目标，按实际显示的列表进行退场。
仓库滑入/滑出动画由已有人员列表动画复制，曲线可继续在 Designer 中编辑；四个动画名称在类默认值中配置。
GetCurrentListAnimation 根据当前状态返回列表动画。蓝图同时播放左右面板动画，各自 Finished 调用 RecordPanelAnimationFinished，
仅两者都完成时返回 true，再由蓝图调用 NotifyLoadCompleted / NotifyUnloadCompleted。
卸载过程中禁用交互、取消待执行点击并锁定展示状态，避免退场动画中途切换列表。
空列表清空详情和背包，不残留上一个人的数据。

默认生成 8 名演示人员（6 名待命、2 名执行中）。bUseMockData 可关闭演示数据。
SetPersonnelData 接入真实展示数据，会排除空 ID 和重复 ID；SelectPersonnel 根据 ID 选人。
OnPersonnelSelected 广播选中 ID，清空时传 None。
装备与训练目前只展示假数据，不会实际修改装备、扣款或启动训练。

通用带 ChoiceID 的单选按钮：UIBasic/SelectionButtonWidget.h 与
`/Game/System/UIBasic/WBP_SelectionButton`。沿用鼠标按下即播放音效、两帧后触发反馈的基础按钮机制。
单选页签使用原生 Slate 从底部向上渐隐的蓝色渐变和底部细线，支持悬浮和选中反馈。
bUseTabStyle 控制是否使用此样式；人员列表项关闭它，保留卡片边框。
人员专用列表项：
`/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/Components/WBP_PersonnelListEntry`。
展示数据结构 PersonnelViewData.h 放在人员整备室自己的 C++ 目录中，不占用正式单位数据结构。

场景资产迁移保留 UE 重定向器以兼容已有蓝图引用。
验证：SilverChoir.BaseUI.Personnel，检查筛选/页签按钮绑定、选择保持、空数据处理与三页截图。

## 作战指挥室

界面仍使用 `WBP_OperationsCommandRoom`，继承 `UOperationsCommandRoomWidget`。基地公共顶栏、底栏和返回按钮由宿主管理，房间内容仅占右侧 360 个设计单位。`TimeControlPanel` 显示日期、时钟、当日进度与正常/快进按钮；`TileInfoPanel` 初始隐藏，选中本世界的基地沙盘瓦片后显示，清空选择时隐藏。`TileInfoContent` 是空的 Designer 容器，预留后续地图信息，不包含演示数据。

时间使用既有 `GameTimeSystem` 的 GameInstance 时钟。正常为 1 倍速，快进默认 4 倍速，可在控件类默认值 `FastForwardScale` 修改；两个按钮成功设置倍率后恢复时钟推进。打开、关闭和切换房间仅管理订阅，不重置游戏日期或当前倍率。外部变速、暂停和改时会同步更新界面；不修改引擎 TimeDilation。日期与时钟显示格式可在 `DateFormat`、`ClockFormat` 配置。

地图选择沿用网格插件，`GetSelectedTile()` 提供当前 `UGSMTileData`；未来可以在空信息容器中绑定真实数据。面板背景与控件拦截鼠标按键、双击和滚轮，左侧地图区域及未显示信息面板时的右下空白保持可操作。

作战指挥室左键输入在 `BP_GameMainMapPlayerController` 的“作战指挥室|地图输入”分类中编辑。`CommandMap_BeginPointer` 调用插件 `BeginDragMapWithMouse` 并保存按下位置与瓦片；`CommandMap_UpdatePointer` 超过 `CommandMapDragThreshold`（默认 6 个视口像素）后调用 `DragMapToMousePosition`；`CommandMap_ReleasePointer` 先补算最终位移，只有未拖动且在同一瓦片松开才调用 `HandleTileClickRequest`；`CommandMap_CancelPointer` 结束拖拽并清空状态。拖动后回到起点仍按拖动处理。移动的是地图内容，沙盘底座、相机和既有滚轮缩放保持原行为；插件继续限制内容边界，完整适配凹槽的最小缩放状态保持居中，放大后可拖动浏览。

沙盘命中查询、阈值、拖拽和抬起选择现在全部由控制器蓝图声明和实现，入口位于 `MouseEventGraph`。C++ 只负责通用/基地/战斗映射生命周期及通用输入重置通知。详细节点与配置见 `Docs/MainMapInput.md`。

房间装饰 `StaticMeshActor_1986` 会挡住普通 Visibility 鼠标射线。指挥室内先用沙盘凹槽平面确定目标地图，再通过插件 `GetTileUnderMouse` 对本地图的有效瓦片做精确碰撞拾取；只在按下和抬起时查瓦片，逐帧拖动只查凹槽位置。中键与滚轮也使用同一房间凹槽目标查询，其他场景保留原来的射线规则。结束拖拽后，由控制器蓝图消耗本次插件点击抑制，防止误丢弃下一次正常点击。

2026-10-01 左键输入验证：Editor Development 与玩家控制器蓝图编译通过；`Saved/CommandMapInput/PointerFinal/index.json` 的真实输入测试通过，覆盖微小抖动、抬起选择、跨阈值拖动、拖回起点、无中间帧的快速抬起、UI吞掉抬起、UI按下后地图抬起、失焦取消、退出取消和点击抑制清理。`RoomRegression/index.json` 的指挥室流程测试与 `MiddleRegression/index.json` 的两项中键测试通过，共 4 项通过、0 警告、0 失败。最初射线被场景模型挡住的失败记录保留在 `PointerDiagnostic`；最终通过版本使用地图自身瓦片拾取。项目原有 ToolsetRegistry 启动错误仍单独出现在启动日志中，不计作上述目标测试结果。

布局维护工具 `OperationsCommandRoomUIAssets -Apply` 只在原 `ContentRoot` 为空时创建布局；已创建后再次运行会保留 Designer 修改。原资产备份保存在 `Saved/OperationsCommandRoomUI/Backups`。

`BP_作战指挥室_Scene` 和小队会议室一样直接继承 `USMS_SceneBase`。进入时等待相机和界面均完成，再开放返回按钮；离开时卸载界面并恢复进入前状态。`InteriorCameraState`、`RoomMinPitch`、`ReturnCameraState`、`UiFinish`、`CameraFinish`、`FlowStage`、UI/相机缓存、原俯仰范围、操作锁及区域可见性都由蓝图自身持有。旧原生场景类仅留空兼容类型，不再声明流程节点。

网格插件通过 `AGSMMap3D::OnSelectedTileChanged` 通知界面，明确处理切换、清空、选中瓦片销毁和地图重建，并防止蓝图回调重入后发布过时状态。选择监听与时间监听在界面卸载时解除，待执行按钮点击同时取消。

### 在蓝图中编辑作战指挥室流程

控件事件在 `/Game/System/Map/BaseMap/UI/SceneUI/OperationsCommandRoom/WBP_OperationsCommandRoom` 中编辑：加载与卸载、正常/快进按钮点击、时间/倍率/暂停变化、瓦片选择变化。C++ 提供订阅准备、释放、时间显示刷新与地图数据查询接口；流程顺序和面板显隐由蓝图节点决定。

`加载场景UI` 先准备订阅并刷新初始状态，再调用 `通知加载完成`；`卸载场景UI` 先释放订阅、取消待执行点击并禁用交互，再调用 `通知卸载完成`。需要入场/退场动画时，将对应完成通知接到动画完成回调，C++ 不会提前代为完成。保持卸载开始时释放订阅，避免退场中的旧控件继续响应地图与时钟。

场景流程在 `/Game/System/Map/BaseMap/Scene/BP_作战指挥室_Scene` 中编辑：`EnterScene`、`LeaveScene` 与相机/UI完成事件均有实际蓝图节点。两侧完成汇合后再通知场景管理器；离开完成仍延后一帧执行，以便宿主先移除旧控件。`Room_CacheCamera` / `Room_RestoreCamera` 保存和恢复相机与操作锁，`Room_HideRegions` / `Room_RestoreRegions` 保存和恢复区域显示；这些 helper 也是蓝图函数。原 `InteriorCameraState` 和场景标签继续保留。

进入镜头分两段：`MoveRoomCameraToLocation` 保持进入前的臂长、俯仰和朝向，只移动到 `InteriorCameraState.Location`；`OnRoomCameraPositioned` 蓝图事件随后隐藏区域标记、加载右侧界面，并调用 `MoveRoomCamera(true)` 缩进到完整的 `InteriorCameraState`。第一段结束不会提前通知场景进入完成。位置、最终视角与三项移动速度仍在场景蓝图默认值中配置。

进入时通过相机实例的 `GetPitchLimits` 保存实际运行时范围，将俯视下限临时扩展至 -90°（原本更宽则保留），上限保持原值。离开先回到进入前视角，再恢复原俯仰范围和操作锁，不修改全局相机配置。-90° 是允许范围，不会强制覆盖蓝图配置的最终俯仰角。

控件新增事件图 `OperationsCommandRoomEvents`，瓦片信息显隐函数为 `RefreshCommandTileInfoPanel`；原 `EventGraph` 的空 PreConstruct 事件接入按钮文字刷新。场景新增事件图 `OperationsRoomLifecycleEvents`，进入、离开分别位于 `EnterScene`、`LeaveScene` 函数图；节点已加中文说明。

一次性迁移工具分别为 `OperationsCommandRoomWidgetEvents -Inspect/-Apply` 和 `OperationsCommandRoomSceneEvents -Inspect/-Apply`。它们备份资产后增补节点、编译成功才保存；检测到目标事件已存在用户逻辑时拒绝覆盖，已迁移后保留后续修改。原布局、动画和相机默认值保留。备份分别位于 `Saved/OperationsCommandRoomBlueprintEvents/Backups` 与 `Saved/OperationsCommandRoomSetup/SceneEventsBackups`。

`OperationsCommandRoomSceneEvents` 同时支持相机流程 v2 升级：对已知旧版 EnterScene 的三个连续调用作局部替换，新增 `OnRoomCameraPositioned` 事件；其他图、场景标签和完整 `InteriorCameraState` 默认值保留。`-Inspect` 输出升级计划及其他房间的相机配置作参考，`-Apply` 在备份后保存；已升级时再次执行不会重写资产。

动画等待契约测试为 `SilverChoir.BaseUI.OperationsCommandRoom.BlueprintLifecycle`。运行前先执行 `OperationsCommandRoomWidgetEvents -CreateLifecycleFixture`，将当前正式控件复制至 `Saved/OperationsCommandRoomBlueprintEvents/Fixtures`，只断开副本中两处完成通知节点的执行输入。测试临时挂载 `/OperationsTest/` 并替换当前宿主实例的房间类，验证加载/卸载可等待、相机与界面完成汇合、重复通知无效、退出下一帧收尾及订阅释放；结束恢复映射并卸载挂载点。正式资产不变。此测试与 `RuntimeFlow` 都需从 MainMenu `-game` 各自单独运行。

蓝图迁移最终验证：C++ 与两个正式蓝图编译通过，`RuntimeFlow` 和蓝图副本的 `BlueprintLifecycle` 均为 1 项通过、0 警告、0 失败，报告分别位于 `Saved/OperationsCommandRoomBlueprintEvents/RuntimeFlow/index.json`、`BlueprintFixtureFlow/index.json`。重复运行两个迁移工具后，正式资产 SHA-256 均保持不变。早期反射拦截测试未能暂缓事件，已以实际蓝图副本测试取代，最终测试没有使用反射拦截。

2026-10-01 相机流程 v2 验证：Editor Development 编译通过，`Saved/OperationsCommandRoomCamera/RuntimeFlow/index.json` 与 `BlueprintLifecycle/index.json` 各 1 项通过、0 警告、0 失败。两次进入分别采样到 47/46 帧定位过程，缩进与角度漂移均为 0；最终达到蓝图配置的 -90°。退出分别恢复运行时范围 `(-80, 15)` 与 `(-72, -18)`。保留当前作者配置的位置 `(0.116914, 79.871881, 100)`、臂长 600 与缩进速度 8000。运行截图位于 `Saved/OperationsCommandRoomCamera/operations_top_down.png`。工具启动日志仍有既有 ToolsetRegistry Python 注册错误，以上统计指目标自动化测试结果。

验证入口 `SilverChoir.BaseUI.OperationsCommandRoom.RuntimeFlow` 从 MainMenu 真实新游戏流程进入基地，覆盖进入指挥室、正常/快进、外部暂停同步、瓦片选择及空面板、面板输入拦截、返回与再次进入，以及时钟倍率保留和事件解绑。必须以 `-game` 单独运行该测试。添加 `-OperationsVisualReview` 可输出选中前后截图。网格插件的 `GSM.` 测试共 11 项，最终报告 `Saved/OperationsCommandRoomUI/GridTestsFinal/index.json` 为 11 项通过、0 警告、0 失败。

2026-10-01 实测：Editor Development 构建成功；真实游戏流程在 1920×1080、2560×1080 均通过，报告分别为 `Saved/OperationsCommandRoomUI/RuntimeReview/index.json`、`RuntimeReviewWide/index.json`。对应目录中的 `operations_selected_1920x1080.png` 与 `operations_selected_2560x1080.png` 为实际游戏截图。

额外执行的共享 `SilverChoir.BaseUI.Shell` 回归中，房间切换和释放断言通过，但公共 HeaderEffects / FooterEffects 的两条绘制断言失败：宿主资产仍引用 `coui://uiresources/BaseShell/header.html`、`footer.html`，当前 `Content/UI/BaseShell/` 资源缺失。该既有资源问题保留在 `Saved/OperationsCommandRoomUI/ShellTests/index.json`，本次未重建公共装饰或移除失败断言。

## 沙盘地形与选中边框（2026-10-02）

沙盘局部材质使用 `SandboxAlbedoGain` 补偿基地固定曝光下的过亮漫反射，并通过 `SandboxSpecular`、`SandboxRoughnessFloor` 减少泛白高光。`/Game/System/Map/BaseMap/Sandbox/Materials/MI_BaseSandboxTerrain` 当前设置为 `0.07 / 0.08 / 0.8`；水面实例 `MI_BaseSandboxWater` 为 `0.12 / 0.08 / 0.5`。调大 AlbedoGain 会提亮地形。模型原始材质、全局曝光和房间灯光保留。

`BP_BaseSandboxTile` 使用 `MI_BaseSandboxGrid`，普通网格保持细淡。插件将选中状态写入每个瓦片独立动态材质的 `SelectedAmount`，驱动选中边宽、不透明度及发光；只有边线参与绘制，瓦片内部不染色。切换与取消选择均恢复旧瓦片。选中时将贴花组件原排序增加“选中边线排序增量”（默认 10），取消后恢复，缩放/刷新不会累计排序。

调整位置：

- `BP_BaseSandboxTile` 类默认值 → “网格策略地图 | 瓦片 | 选择”：默认/选中边线贴花颜色、选中边线排序增量。
- `MI_BaseSandboxGrid` → `SelectedBorderEmissiveStrength`（当前 4）、`SelectedBorderWidth`（当前 0.015）、`SelectedBorderOpacity`（当前 1）；普通状态使用 `BorderEmissiveStrength`、`BorderWidth`、`BorderOpacity`。

应用脚本 `Scripts/Editor/refine_sandbox_appearance.py` 在保存前备份七个目标资产到 `Saved/SandboxAppearance/Backup_*`。该脚本重新应用本次参数预设，日常调参直接编辑上述实例即可。材质创建器 `base_sandbox_materials.py` 同步支持选中边框。

`RuntimeFlow` 验证选中/切换/清除、缩放后的独立边框状态和排序恢复；`-OperationsVisualReview` 输出普通、选中、缩放、清除及雪山/水域截图。鼠标实际输入回归报告为 `Saved/SandboxAppearance/PointerRegression/index.json`（1 项通过，0 警告，0 失败）。截图位于 `Saved/SandboxAppearance/After/`。

最终 Editor Development 编译通过，`Saved/SandboxAppearance/FinalReview/index.json` 为 1 项通过、0 警告、0 失败。实机截图确认草地、雪山和水域均可辨认选中边框，取消选择后恢复普通网格。上述统计指目标自动化测试；工具启动阶段既有 ToolsetRegistry Python 注册错误仍单独记录在日志中。

## 人员整备室返回基地（2026-09-27）

`BP_人员整备室_Scene` 进入时必须同时收到相机与 UI 完成通知，才能调用 `NotifyEnterCompleted`。如果 `CreateSceneUIByTag` 复用了已经加载的界面，`OnLoadCompleted` 不会重新广播：绑定完成事件后还需检查 `IsSceneUILoaded`，为 true 时调用现有“人员整备室界面加载完成”函数。不要对仍在播放入场动画的 UI 提前调用通知。

此前缺少这个检查，实测出现 `IsSceneUILoaded=true`、`UiFinish=false`、`CameraFinish=true`，管理器一直处于 Entering。此时返回按钮虽然有点击反馈，`SwitchScene` 仍会因场景忙碌拒绝返回请求。

进入和离开时隐藏返回按钮；在“人员整备室全部加载完成”汇合处显示，再通知进入完成。房间相机状态、手动相机锁、人员/库存业务以及 WBP 动画保持原来的配置。

自动化验证前缀：`SilverChoir.BaseUI.PersonnelReturnFlow`。覆盖档案页重复进入/返回、仓库返回、入场中切换装备页、已加载 UI 复用。测试通过真实 `BP_BaseMapWidget` 按钮事件及全局场景管理器运行。

本次四项运行测试全部通过（含五次完整返回），日志 `Saved/Logs/PersonnelReturnAfter.log`。修复前的复现记录在 `Saved/Logs/PersonnelReturnBefore.log`，其中 PreloadedUI 等待 30 秒仍未完成进入。验证直接运行 GameMainMap 的真实场景蓝图，不包括主菜单的地图加载过程。

修复工具为 `PersonnelSceneReturnRepair -Repair`，只为这次已有节点结构插入补偿和可见性调用，不重建用户图表；保存前备份到 `Saved/PersonnelSceneReturnBackups`。已修复后不要重复运行 Repair，使用 `-Inspect` 查看原图、相机局部默认值和事件绑定。C++ 与蓝图编译成功；修复命令进程仍会报告原有 EasyHouseBuilderEditor 工具注册错误，但日志包含 `PERSONNEL_RETURN_REPAIR_OK`，运行测试进程退出码为 0。
