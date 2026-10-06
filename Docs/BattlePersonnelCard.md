# 战斗人员卡片

资产：`/Game/System/Map/BattleMap/UI/Components/WBP_人员卡片`。

父类为 `UBattlePersonnelCardWidget`，继承既有 `UBattleMemberCardWidget`，可赋给战斗 HUD 的 `MemberCardClass`。本次先完善独立卡片，现有 `WBP_BattleHUD` 的类配置未改动。

## 保留的布局

- 主体 `SizeBox_202`：230×144；根 Border 的 2 像素边距保留，总尺寸 234×148。
- 头像容器 `SizeBox_0`：80×80，仍使用统一正方形头像裁剪。
- 原心电图控件与库存插件 `WBP_MirrorSlotContainer` 保留。
- 右侧零宽扩展容器和原两段 UMG 动画保留。原“点击人物面板”函数的鼠标绑定已解除，防止与 C++ 的抬起选择路径重复执行、改变卡片宽度。

状态条改为 `UBattleResourceBar`：7 像素宽，纵向渐变、暗轨道、细边、低对比刻度、顶部亮帽，体力蓝色、士气绿色。不显示数字或额外标签；0 值不留填充高光。使用原生 Slate 属性失效，无常驻 Tick 或 H5 页面。

未选中时使用深色底与细框，悬浮时略微提亮；选中时使用青色描边、内侧微光及两处角标。所有绘制都位于原矩形内，选择不会缩放控件或改变布局。

原根 Border 的绘制已关闭，保留其边距，避免旧描边遮挡原生选中框。装备槽使用低对比蓝灰边框和标签；样式只应用于本卡片的镜像槽实例，不修改库存插件的公共资产。

## C++ 展示接口

- `SetMember(FBattleMemberView)`：更新成员身份、头像、名称、生命状态、体力和士气；比例校验到 0–1，非有限值按 0 处理。
- **更新人员生命状态** `SetMemberVitals(Health, Stamina, Morale)`：保留身份与选中状态，只更新三个比例。
- `SetSelected(bool)`：设置卡片的显示选中状态，不修改 canonical `FUnitData` 的真实选择。
- **是否选中人员** `IsMemberSelected()`：读取卡片显示状态；点击业务以共享单位数据的 `IsSelected()` 为准。
- **刷新人员卡片样式** `RefreshCardStyle()`：运行时更改 `StaminaColor`、`MoraleColor`、`DisplayHeartRateBPM` 后刷新。
- `bPreviewSelected`：仅用于 Designer 中预览选中效果。

生命值沿用既有展示区间：0 为失能，低于 0.3 为危急，低于 0.7 为负伤，其余为稳定。心电图颜色随展示状态变化，强度随生命比例缩放；0 立即平线。心率默认 72，可通过 `DisplayHeartRateBPM` 调整。卡片不自行判定权威游戏数据。

嵌套 `ECGMonitor` 的 `AnimationSeed` 默认 0：每张卡片自动获得独立种子，让相同心率的成员错开波峰及扫描位置。`SetMember` / `SetMemberVitals` 不会重新播种。如需复现特定成员的动画起点，可在蓝图中对 `ECGMonitor` 调用 `SetAnimationSeed` 并传入非 0 整数。

## 蓝图业务入口

`PersonnelBusiness` 事件图保留两个接线入口：

- **人员数据更新后** `OnMemberDataApplied`：在子控件构造完毕后通知，可以为 `HandEquipment` 接入真实单位的库存。先 `SetMember` 再加入界面时，会延迟到构造完成再通知，避免镜像槽尚未创建。
- **请求操作此人员** `OnMemberInvoked(UnitId)`：完成一次有效点击后通知业务。可接人物定位、业务控制面板等功能。

人员卡的有效点击先取消交互目标选择，再读取单位管理器持有的 canonical `FUnitData::IsSelected()`。已经选中的人物保留全部单位当前选择，包括多选中的其他人物，只正常发送面板请求；尚未选中的人物才调用玩家单位选择组件的 `SelectUnitById` 切换为单选。即使按钮显示状态暂时过期，也以共享单位数据为准。例如 A、B 同时选中时，点击 A 或 B 都保留二者，点击未选中的 C 才变为仅选 C。

`OnControlPanelRequested(Card, UnitId)` 每次有效点击都广播，包括已选中人物，最后通知 `OnMemberInvoked`；打开面板不需要额外的选择变更通知。点击过程中若取消 targeting 或选择回调重建、销毁了卡片，生命周期检查会停止旧卡片后续通知。现代人员卡不再通过旧 `OnSelectionRequested` 路由打开 HUD 抽屉。相同成员刷新数据不会丢失选中状态。

`ControlPanelBusiness` 图提供 **打开控制面板** `OnControlPanelOpened(UnitId, SquadId)`、**关闭控制面板** `OnControlPanelClosed(UnitId, SquadId)`。父成员模式负责先关闭旧人物/车辆，再打开点击的人物。这里由蓝图实现已有扩展区域的布局和滑动动画；不必再次调用单位选择节点。`bControlPanelOpen` 表示逻辑上的展开状态，与角色是否选中独立。

`HandEquipment` 是原生类型 `USIS_MirrorSlotContainer`，可以直接连接库存插件节点。保留原左右手配置、合槽能力、鼠标输入与拖放行为；`FBattleMemberView` 的测试装备 ID 不会被转换成真实物品。库存绑定和物品操作需在蓝图中接入。

原 Designer 控件重命名为 `Portrait`、`MemberName`、`HealthText`、`StaminaBar`、`MoraleBar`、`ECGMonitor`、`HandEquipment`，以匹配原生绑定。外部父容器仍决定最终分配空间；接入 HUD 时不要把固定卡片压缩到 234×148 以下。

## 验证与恢复

测试：`SilverChoir.BattleUI.PersonnelCard.RuntimeFlow`。检查真实尺寸、控件绑定、比例边界、鼠标单次选择、选中保持、死亡平线、镜像槽命中和库存数据不变；截图输出到 `Saved/Screenshots/PersonnelCard/01_States.png`。

原资产备份位于 `Saved/PersonnelCardUpgrade/Backup/<时间>/WBP_人员卡片.uasset`。只读结构检查：`-run=BattleHUDAssets -CardInspect`；可重复应用本次样式：`-run=BattleHUDAssets -CardUpgrade`（会先备份）。

2026-10-04 修复 `Border_1` / `Border_2` 的 `OnMouseButtonDownEvent` 残留绑定：它们的旧函数 GUID 无法解析，编译显示目标为 `None`。仅移除这两条失效记录，保留 Designer、动画和业务图。精准修复命令为 `-run=BattleHUDAssets -CardBindingsRepair`，检查命令为 `-CardBindingsInspect`；备份在 `Saved/PersonnelCardBindings/Backup/`。资产重新加载后编译 0 错误、0 警告；`SilverChoir.BattleUI.PersonnelCard.RuntimeFlow`、`SilverChoir.Units.Selection.RuntimeFlow` 均通过，日志为 `Saved/OperationsPanelUpgrade/CardBindingsColdVerify.log` 和 `CardBindingsRuntime.log`。
