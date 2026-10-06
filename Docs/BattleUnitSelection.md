# 战斗单位框选与共享选择

`BP_GameMainMapPlayerController` 的 `BattleUnitSelection` 图将现有 `IA_MouseLeft` 的 Started / Completed / Canceled 分别接到原生 `UnitSelection` 组件的开始、完成、取消节点。原有基地沙盘输入保留。算法和选择框绘制由 `UBattleUnitSelectionComponent` 负责，蓝图可继续连接业务。

战斗地图内左键拖拽超过默认 6 像素后显示细青色选择框，并在拖动过程中实时替换当前选择：进框即选中，离框即取消，空框清空选择。松开时使用最后的鼠标位置确认，不先回滚。短按可点选单位。任意方向拖拽有效，默认胶囊的屏幕包围框与选择框相交就算选中，可打开“必须完整框住单位”。未显示、禁止选择、相机后方及其他已加载子地图中的单位不参与框选。持久关卡内动态生成的单位仍可参与。暂未加入阵营/视线遮挡规则。

鼠标从 HUD、背包等界面开始操作，或处于战斗交互目标模式、地图加载过程时，不启动框选。拖动进入 UI 时暂停实时更新；在 UI 上抬起、丢失指针或输入取消，恢复起拖时仍有效的选择。切换地图或移除地图 UI 会结束手势并清空选择，不恢复旧地图或已销毁单位。单位与数据通知只在实际状态变化时触发。

## 单位数据及蓝图接口

`FUnitData` 的 `SetSelected(bool)` / `IsSelected()` 维护瞬时选中状态，`OnSelected` / `OnDeselected` 携带 `UnitId`。重复设置不重复发事件。选择状态和事件订阅不写入存档或复制到快照；已有权威数据原地更新时保留选择和订阅。

C++ 对同一个 `TSharedPtr<FUnitData>` 使用 `AddUObject` 和 `FDelegateHandle` 订阅，换绑和销毁时解除。不要对 `GetSnapshot()` 返回的结构体副本操作选择状态。

蓝图消费者的接法：

1. 调用 **根据ID获取单位数据引用**，将返回的 `UUnitDataReference` 保存为成员变量。
2. 绑定引用的 **单位被选中**、**单位取消选中** 通知。
3. 绑定后读取引用的 **单位是否选中**，同步初始显示；订阅不会回放历史事件。
4. 销毁或换绑消费者时解除自身的绑定，并释放旧引用。

`AUnitPawnBase` 已自动绑定自己的共享数据，并提供 `SelectionDecal` 原生贴花组件。原生代码在蓝图选中通知前同步贴花显隐；子蓝图的 **单位被选中** / **单位取消选中** 可增加材质或描边表现。默认使用 `/Game/System/Object/Unit/Materials/M_UnitSelectionDecal` 的细青色环，子蓝图可调整组件尺寸、脚下相对位置和材质，材质参数包括 RingColor、RingWidth、RingOpacity、GlowStrength。未绑定数据的测试 Pawn 也支持本地选择；`允许被选中` 默认为 true。展示台单位不显示战术选中圈。

`WBP_人员卡片` 的原生基类订阅同一共享状态，框选会同步卡片外观。新创建或重新添加的卡片会同步当前值。点击由 C++ 先结束交互目标选择，再读取单位管理器持有的 canonical `FUnitData::IsSelected()`，不以按钮显示状态作为权威。已选中人物的点击保留全部单位当前选择，只请求打开该人物面板；未选中人物的点击才调用 **根据单位ID单选单位**（`SelectUnitById`）。因此 A、B 多选时点击 A/B 不改变选择，也不增加选择变更通知；点击未选 C 则切换为仅选 C。

每次有效点击仍向成员模式发送控制面板请求，最后通知 `OnMemberInvoked` 业务；重复点击按原规则关闭再打开面板，关闭面板不会取消单位选择。生命周期检查继续防止回调重建或销毁卡片后发送过期请求。旧 `UnitSelectionBusiness` 中重复调用选择组件的生成节点已移除，原有业务分支保留。即使 Pawn 尚未生成也可以选择数据，后来生成的 Pawn 会立即同步；下一次框选或清空会取消该选择。

组件提供 **获取已选单位**、**设置所选单位**、**清空单位选择**、**查询框内单位**。查询节点使用视口像素坐标。`所选单位已改变` 是组件整体操作完成后的通知；单个数据的外部变更应订阅对应的数据引用事件。

## 验证（2026-10-04）

- `SilverChoir.Units.Selection` 下的 `SharedData`、`BlueprintReference`、`ReloadPreservesSelection`、`RuntimeFlow` 四项用例通过。
- 运行验证覆盖矩形投影查询、选择替换、数据/Pawn/卡片同步、实际人员卡片蓝图点击事件、先选数据后生成 Pawn、销毁与切图清理。
- `SilverChoir.BattleUI.Roster.RuntimeFlow` 回归通过，验证原有小队列表与人员卡片业务。
- 控制器和人员卡片蓝图已编译保存，原输入连线保留；备份位于 `Saved/BattleUnitSelection/Backups`。
- 日志：`Saved/OperationsPanelUpgrade/UnitSelectionRuntime.log`、`Saved/OperationsPanelUpgrade/BattleRosterAfterUnitSelection.log`、`Saved/OperationsPanelUpgrade/UnitSelectionAssets.log`。

上述验证为运行进程自动化，未代替人工鼠标拖拽与选择框视觉验收。资产命令进程存在原有 EasyHouseBuilder Toolset 注册错误；任务涉及的蓝图编译、保存和运行测试均成功。

2026-10-04 实时框选更新已通过 C++ 构建和全部 4 项 Selection 测试。扩展的 RuntimeFlow 使用可控指针驱动真实 Begin/Tick/Complete/Cancel 路径，验证进出框、取消恢复、UI 暂停、最后释放位置、回调清空、原生卡片点击、已选数据后生成 Pawn 及贴花先于蓝图事件更新。贴花材质冷加载/重编译通过。日志 `Saved/OperationsPanelUpgrade/SelectionPanelsRuntime.log`、`PanelAssetsColdVerify2.log`。
