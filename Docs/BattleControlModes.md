# 战斗 UI 控制模式

主界面：`/Game/System/Map/BattleMap/UI/WBP_BattleHUD`，父类仍为 `UBattleMapWidget`。

## 用真实小队初始化

在需要展示参战小队的业务蓝图中：**获取主地图玩家控制器 → Get Battle Widget → 初始化战斗UI**，向 `SquadIds` 传入现有玩家小队的 GUID 数组。返回 `true` 表示成功；失败时读取 `OutError`，原列表和选择保持不变。这只更新界面，不生成战斗单位或加载地图。

节点读取玩家小队、人员及车辆管理器，按数组顺序生成两个模式左侧的小队列表；重复 ID 只取第一次，首次默认选中第一支小队。重复初始化会保留仍在列表中的已选小队。空数组清空列表。无效、不存在的小队或缺失成员会整批失败。

点击任一模式的小队项，由 C++ 保持两个列表一致的单选状态，并将 `MemberModePanel.MemberList` 更新为该队成员。重复点击已选小队不会重建人员卡片。切换成员/小队模式保留选队。小队项使用 `WBP_小队项` / `UBattleSquadEntryWidget`，人员使用 `WBP_人员卡片`；均可在 HUD 的组件类默认值中调整。

`RosterBusiness` 图保留两个空业务事件：**战斗UI小队初始化完成**、**战斗UI选中小队已改变**。这些通知触发时列表与选择已经更新；在这里接镜头定位或单位选择等业务。不要在通知内同步重新初始化或再次切换小队，以免形成循环。

初始化读取真实档案、健康和体力比例、手部装备及车辆数据。当前数据模型没有士气、快捷栏数量和车辆乘员的权威字段，因此这些展示值初始为 0，可由后续业务使用 `UpdateMemberView` 更新。小队模式中央的 `SquadCardList` 继续作为后续摘要卡的预留容器。

## 模式切换

成员、小队按钮的 `ChoiceID` 分别为 `MemberMode`、`SquadMode`，由 C++ 接收点击，调用 `SetControlMode`。默认成员模式；重复点击当前模式不会重置列表滚动位置或重复通知。切换时收起旧指令抽屉、取消旧目标选择，滚动到对应页面，并同步两按钮的互斥选中状态。

`ModePages` 是 `UBattleModeScrollBox`。保留 ScrollBox 的程序滚动动画，屏蔽外层滚轮、右键拖拽、触摸、手柄模拟输入、导航与焦点自动滚动；外层滚动条不参与界面布局。两个页面都保留占位，非当前页禁用交互。内层小队和成员列表可以正常滚动。

蓝图接口：

- **切换战斗控制模式** `SetControlMode(Mode, bAnimate)`：`Mode` 为 `Member` 或 `Squad`，返回是否接受。同步切换期间的跨模式重入会拒绝。
- `CurrentControlMode`：只读当前模式。
- `ModeSwitchInterpolationSpeed`：切页动画速度，默认 18。
- **战斗控制模式已切换** `OnControlModeChanged(PreviousMode, NewMode)`：`ControlModeBusiness` 图中的业务入口，仅实际切换时触发；可以连接单位选择、输入上下文等游戏逻辑。C++ 不在此操作真实单位数据。
- 模式按钮的 **是否选中战斗按钮** `IsSelected()`：读取选中状态。

主界面绑定：`ModePages`、`MemberModePanel`、`SquadModePanel`、`MemberModeButton`、`SquadModeButton`。

## 页面容器

`WBP_成员模式` 的父类为 `UBattleMemberModeWidget`：

| 原控件名 | C++ 绑定名 | 类型 / 用途 |
| --- | --- | --- |
| ScrollBox_小队列表 | SquadScrollBox | 纵向滚动框 |
| 队员模式_小队列表 | SquadList | VerticalBox，动态添加小队项 |
| ScrollBox_成员列表 | MemberScrollBox | 横向滚动框，包含人员列表与预放车辆面板 |
| HorizontalBox_91 | MemberList | HorizontalBox，动态添加人员卡片 |
| 显示车辆信息按钮 | VehicleButton | 车辆按钮业务入口 |
| VehiclePanel | VehiclePanel | 必需 BindWidget，Designer 控件固定使用该名称，直接位于 MemberScrollBox 内 |

`WBP_小队模式` 的父类为 `UBattleSquadModeWidget`，左侧仍为 `SquadScrollBox` / `SquadList`；中央预留横向容器为 `SquadCardScrollBox` / `SquadCardList`，供后续动态添加小队摘要卡。车辆按钮同样绑定为 `VehicleButton`。

成员/小队列表及各自 ScrollBox 原本已通过固定名 `BindWidget` 直接绑定 C++ 引用，`VehiclePanel` 现在同样使用必需的 `BindWidget`。Designer 控件名称必须匹配对应 C++ 属性。这些绑定在蓝图中只读，但容器内容可通过 `Add Child to Vertical Box` / `Add Child to Horizontal Box`、`Clear Children` 等常规节点修改。例如从战斗 UI 的 `MemberModePanel` 取得 `MemberList`，向其中添加创建好的人员卡片。基类不会自动清空或填充列表，现有测试子项保留。

## 人员与车辆控制面板

`UBattleMemberModeWidget` 统一管理 `ActivePersonnelCard`、`VehiclePanel`。人物卡每次有效点击先取消交互目标选择，再读取单位管理器持有的 canonical `FUnitData::IsSelected()`：已经选中的人物保留全部单位当前选择（包括框选形成的多选），只请求打开该人物面板；尚未选中的人物才通过 `SelectUnitById` 切换为单选。按钮的显示选中状态不作为判断依据。切换到不同面板时，父级先调用旧面板的关闭事件，再调用新人物卡的打开事件；重复点击同一上下文中已打开的卡片时，仍通知点击请求和 HUD，但保持当前展开状态，不重复关闭或重启动画。车辆按钮切换车辆面板的开关：关闭时点击会先关闭人物面板再打开车辆，已打开时点击则关闭车辆；显式 `ShowVehiclePanel` 对已打开的同一车辆保持展开。人物与车辆面板保持互斥，无载具时按钮禁用。模式切换、切换小队和重建时关闭旧面板，销毁时解除交互绑定。

车辆基类为 `UBattleVehiclePanelWidget`，子蓝图位于 `Components/WBP_车辆面板`。当前布局把 Designer 预放的 `VehiclePanel` 直接放在 `MemberScrollBox`（原 `ScrollBox_成员列表`）内，旧 `VehiclePanelContainer` 已从该布局删除。实际蓝图中的车辆面板必须固定命名为 `VehiclePanel`，由必需的 `BindWidget` 提供原生引用，保留其 Designer 位置。每次 `NativeConstruct` 只绑定一次面板事件并缓存它是否属于 `MemberScrollBox`；构造时收起，点击直接使用绑定引用和缓存关系，不遍历 WidgetTree，打开时更新当前小队和车辆数据。

`ShowVehiclePanel` 节点仍表示明确打开，不使用按钮的开关语义。成功打开位于成员滚动框中的车辆面板时，先根据缓存关系调用 `ScrollWidgetIntoView` 定位；面板动画完成后，由原生事件通知成员模式，在下一次 Slate 布局更新时按最终尺寸重新定位。回调仅处理当前仍打开、仍绑定且宿主有效的面板，关闭时取消尚未执行的滚动请求。整个过程使用固定引用和原生事件，没有常驻 Tick 或硬编码延迟，保留用户的蓝图开关事件与动画资产。

本轮真实 HUD 窄布局诊断中，车辆面板最终布局宽度为 230，但滚动区域内仅约 115 可见：打开时的单次定位早于展开动画完成，后续变宽部分被滚动框裁切。因此修复针对动画结束后的重新定位，不修改面板宽度或重建用户动画。

后续重复点击诊断还确认了独立的动画冲突：原先同一卡片会在同一帧先关闭再打开，快速切换人员或重开车辆时，正反两段动画会同时修改同一个 `WidthOverride`。车辆蓝图先调用父级隐藏再播放关闭动画，隐藏期间旧动画还可能暂停。两个原生基类现在在切换时停止并刷新本控件旧动画，再调用新的蓝图开关事件；不会停止 ECG 子控件的动画。独立的切换版本与上下文校验避免动画结束回调取消请求后又执行过期的打开事件，清理旧动画时也不会触发车辆滚动定位。动画仍由现有蓝图事件选择与播放；本控件拥有的动画会在面板切换时清理，需持续播放的独立表现应放在子控件中。

2026-10-05 动画互斥验证：新增 `SilverChoir.BattleUI.Panels.AuthoredAnimationOwnership` 使用真实 HUD、人员卡片蓝图及 6 个有效单位数据，逐帧检查正反动画互斥，并独立断言人员展开宽 250、车辆展开宽 230，覆盖展开中重复点击、已展开重复点击、人员 A/B/A/B 切换、车辆快速关开和跨面板切换。修复前日志 `Saved/OperationsPanelUpgrade/PanelAnimationBefore.log` 在人员 Stage 5 与车辆 Stage 9 记录两条动画同时播放；修复后该测试及 `AuthoredVehicle`、`RuntimeInteraction`、`VehicleReopenGeometry`、`Roster.RuntimeFlow` 共五项通过。构建及运行日志分别为 `PanelAnimationFixBuild.log`、`PanelAnimationFixedRuntime.log`（同目录）。这次检查只要求当前打开面板到达终点，未要求离屏人员关闭动画归零，因此不足以排除整个列表的残余占位；下述整表回归已补齐该缺口。当时未修改蓝图资产。

2026-10-05 整表占位修复：`PanelGapBefore.log` 在车辆／人员／车辆快速切换后复现关闭人员面板残余宽度 88.48，令六人列表从 1404 增至 1492.48。根因是滚动框裁掉人员卡后 UMG 停止它的动画更新。`FPanelAnimationTick` 在面板蓝图启动有限开关动画后，临时通过 Slate PostTick 向 UMG 序列管理器保留更新资格，仍由引擎每帧推进一次时间；动画结束、控件隐藏或析构即停止订阅。无需硬编码时长，也不直接设置蓝图动画的宽度终点。关闭人员面板必须回到零宽，离屏也不例外；整块已 Collapsed 的车辆面板不占布局空间。

同次诊断发现 `WBP_成员模式` 中预放 `VehiclePanel` 实例的 `Padding.Left=230`，使车辆内容宽 230、外层占位却达 460。窄修复只将此实例左内边距清零，保留所有业务图、动画、其他控件和布局。入口 `-run=BattleHUDAssets -PanelLayoutRepair` 默认只读，`-Inspect` 输出诊断，`-Apply` 仅接受原值 230 并备份后保存成员模式，`-Validate` 冷编译成员模式及 HUD、不保存。备份与快照在 `Saved/BattlePanelLayoutRepair/Backup/20261004-165258-12F749974403B3EAC6836EA8B6E954C3/`；车辆、人员及 HUD 资产未保存。

成员滚动区域使用 `ClipToBoundsAlways`：库存槽名称使用 `ClipToBoundsWithoutIntersecting`，原先能跳过普通父裁剪，将“左手／右手”画到左侧小队列表上。仅成员列表建立强制裁剪边界，保留库存插件其他场景的裁剪配置。

本次最终验证：`PanelGapFinalBuild.log` 构建成功；`PanelGapFinalRuntime.log` 中 `AuthoredAnimationOwnership`、`AuthoredVehicle`、`RuntimeInteraction`、`VehicleReopenGeometry`、`Roster.RuntimeFlow`、`ControlModes.RuntimeFlow` 六项通过。严格断言所有关闭人员面板（含离屏）宽度归零且动画结束、六人列表宽度恢复 1404、车辆内容及外层均为 230 且完全可见，并检查运行中 UMG/Slate 成员滚动框的强制裁剪边界。首次展开、重开、跨面板反转等五个阶段截图位于 `Saved/Screenshots/BattleVehiclePresentation/PanelLayout*.png`，修复前对应图在 `BeforeGapFix/`。人工查看最终反转截图，确认车辆前的空白、成员残余占位和小队区域内的装备标签越界均消失。`PanelGapValidate.log` 冷加载编译通过，命令进程仍有既有 EasyHouseBuilder Toolset 注册错误，不是本次蓝图错误。

2026-10-04 重开裁切修复验证：C++ 编译成功；`Panels.AuthoredVehicle`、`Panels.RuntimeInteraction`、`Panels.VehicleReopenGeometry`、`Roster.RuntimeFlow` 四项运行测试通过。真实 HUD 的溢出布局中，首次展开、三次完整关闭后重开及一次快速关闭/重开均显示完整 230 宽度。新增几何测试要求实际存在滚动溢出，避免宽屏空间足够时误判通过。构建与运行日志为 `Saved/OperationsPanelUpgrade/VehicleReopenFixBuild.log`、`VehicleReopenFixedRuntime.log`，修复后截图为 `Saved/Screenshots/BattleVehiclePresentation/VehicleReopened.png`。成员模式与车辆面板蓝图文件哈希保持不变。

可选的旧 `VehiclePanelContainer` 与 `VehiclePanelClass` 动态创建路径仅用于兼容纯 C++ 构建、尚未提供面板引用的布局；实际蓝图必须提供固定名 `VehiclePanel`，不能依赖该回退路径替代绑定。车辆蓝图提供 Designer 根和事件入口，内容与动画由业务蓝图开发，不固定占用宽度。

成员模式析构时关闭面板并解除订阅；自行创建的动态实例会从父容器移除并清理引用，Designer 预放实例则保留在宿主的 WidgetTree 中。同一个成员模式重新加入界面时会重新绑定并复用原实例，不会删掉预放控件或再创建一份。预放实例的最终生命周期由宿主 WidgetTree 管理。

- 人物卡 `ControlPanelBusiness`：**打开控制面板** / **关闭控制面板**，参数 `UnitId`、`SquadId`。
- 车辆面板 `ControlPanelBusiness`：**打开车辆面板** / **关闭车辆面板**，参数 `InSquadId`、`VehicleId`，完整展示数据可读取 `Vehicle`。
- 车辆关闭是 `BlueprintNativeEvent`，现有事件调用父级立即隐藏。制作收起动画时，将父级调用移到动画完成之后；原生互斥状态已先设为关闭，不必等动画完成。
- 外部动态添加人员卡后调用成员模式的 **注册人员卡片**；HUD 原生初始化/重建和 Designer 预放卡已自动注册。重新赋予卡片不同单位时，只关闭旧上下文，保留绑定。
- 可调用 **关闭当前控制面板** 收起当前内容；也可读取 `ActivePersonnelCard`、`VehiclePanel.bPanelOpen`。关闭不等于取消单位选择。

关闭回调中同步请求打开另一个面板会被拒绝；清空、销毁、重建等生命周期变化会中止尚未执行的打开操作，避免过期引用。先关闭再打开的业务尽量交给父成员模式。

车辆按钮使用 `WBP_BattleVehicleButton`：18×18 的现代 SUV 正面透明 PNG 图标、纵向文字及青色选中反馈，保留原按钮外部尺寸和位置。源图为 `Content/UI/Battle/Icons/VehicleFront.png`，纹理为 `/Game/System/Map/BattleMap/UI/Textures/T_VehicleFront`。车辆默认信息未接入旧 HUD 测试抽屉。

## 样式

两页的 `ListStyle` 可调整轨道、滑块、悬浮和拖动颜色，以及横纵滑块尺寸。默认纵向 4、小队页横向 3；成员列表横向为 2，并始终显示滑块和轨道（内容未溢出时也显示）。使用圆角深色轨道、蓝色滑块、青色悬浮与拖动反馈。运行时修改后调用 **刷新列表样式** `RefreshListStyle()`；不会重置列表内容和位置。

专用 `WBP_BattleModeButton` 保持 26 宽按钮栏内的布局，使用生成的成员 / 小队透明图标和纵向文字。图标通过继承的 `IconBrush` 配置，显示尺寸 18×18；源图与生成提示词位于 `Content/UI/Battle/Icons/`。选中时图标变亮并显示青色填充与底部强调线。底部栏原 160 高度保留。

`WBP_小队项` 为紧凑的 170×52 按钮，展示队徽、队名、成员数和序号，具有悬浮、按下、声音及选中青色强调线。音效复用主菜单的电子悬浮音与终端点击音，可在按钮默认属性中替换。

基础按钮 `UBasicButtonWidget` 的普通文字 Tooltip 已统一为深蓝半透明底、细青色边框、短强调线和浅色文字，成员/小队切换、车辆、小队项等继承按钮自动使用。内容继续填写原生 `Tool Tip Text` 或调用 `Set Tool Tip Text`，支持中文、多行和自动换行。样式在 **按钮 → 悬浮提示 → 提示样式** 中调整颜色、字号和最大宽度（默认 12 / 320）；关闭 **使用游戏提示样式** 可恢复原生显示。显式自定义的 `Tool Tip Widget` 保留原来的内容和样式。普通文字提示默认需要鼠标静止 1.5 秒才允许弹出，随后保留 Slate 原生淡入；移动或离开会收起并重新计时。可在提示样式中调整 **鼠标静止等待时间**，设为 0 时使用原生延迟。计时使用真实时间，游戏暂停不影响它。屏幕边缘定位继续交给 Slate。

悬浮提示已通过 `SilverChoir.UI.GameToolTip.Runtime`：验证原生文本 Setter、缓存复用、空文本、清除提示、自定义内容和样式开关；`SilverChoir.UI.GameToolTip.StationaryDelay` 验证默认 1.5 秒静止等待、移动与重入重计时、禁用按钮提示和等待期间不泄漏原生白色提示。构建与测试日志为 `Saved/OperationsPanelUpgrade/GameToolTipDelayBuild.log`、`GameToolTipDelayRuntime.log`。真实 Slate 样式截图位于 `Saved/Screenshots/GameToolTip/01_GameToolTips.png`。

## 验证与恢复

运行测试：`SilverChoir.BattleUI.ControlModes.RuntimeFlow`，覆盖实际按钮输入、模式对齐、互斥选中、外层输入隔离、内层列表滚动、动态容器、快速切页及库存控件命中。

资产修改前备份到 `Saved/BattleModes/Backup/<时间>/`。只读结构检查：`-run=BattleHUDAssets -ModeInspect`；应用迁移：`-run=BattleHUDAssets -ModeUpgrade`。迁移保留用户当前布局、测试项和其他业务图。

2026-10-04 验证：C++ 编译成功，真实资产加载后的 `RuntimeFlow` 测试通过。日志分别为 `Saved/OperationsPanelUpgrade/BattleModesBuild.log` 和 `Saved/OperationsPanelUpgrade/BattleModesRuntime.log`。成员、小队页面截图保存在 `Saved/Screenshots/BattleModes/01_Member.png`、`02_Squad.png`；截图使用未加载场景内容的测试环境。

同日小队初始化更新已通过 C++ 编译、`SilverChoir.BattleUI.Roster.RuntimeFlow` 和控制模式回归测试。验证了真实管理器数据解析、Slate 点击、单选同步、切队卡片 ID/姓名、重复/重排/空/非法输入，测试结束恢复原管理器。日志为 `Saved/OperationsPanelUpgrade/BattleRosterBuild.log`、`BattleRosterRuntime.log`、`BattleModesAfterRosterRuntime.log`。截图在 `Saved/Screenshots/BattleRosterUI/`；测试档案未设置头像，因此截图中的头像留空。

2026-10-04 人员/车辆面板更新：`SilverChoir.BattleUI.Panels.RuntimeInteraction` 通过，覆盖重复点击、先关后开、车辆互斥、延后赋予预放卡单位数据、targeting 时切换人员、切队/模式清理和回调重入。原有 Roster、ControlModes 回归通过，5 个相关蓝图冷加载/编译及车辆类配置验证通过。构建日志 `Saved/OperationsPanelUpgrade/SelectionPanelsBuild4.log`，最终面板运行日志 `PanelInteractionFinalRuntime.log`，回归日志 `SelectionPanelsRuntime.log`，冷加载日志 `PanelAssetsColdVerify2.log`。编辑器仍存在既有 EasyHouseBuilder Toolset 注册报错，不属于本次蓝图编译错误。

资产备份位于 `Saved/BattlePanelInteraction/Backup/`；迁移入口为 `-run=BattleHUDAssets -PanelInteractionUpgrade`，保留原布局与人员点击业务。车辆关闭事件默认连接父实现，可由用户修改为动画完成后收起。

2026-10-04 车辆预放与多选保留更新：`SilverChoir.BattleUI.Panels.RuntimeInteraction`、`SilverChoir.BattleUI.Panels.AuthoredVehicle`、`SilverChoir.BattleUI.Roster.RuntimeFlow` 均通过。覆盖已选卡保留多选、未选卡单选、显示状态过期时以共享数据为准，以及真实 `WBP_成员模式` 预放车辆的重复打开、人员互斥、Slate 重新挂载和动态后备实例清理。实际选中按钮截图为 `Saved/Screenshots/BattleVehiclePresentation/AuthoredVehicleSelected.png`。

本次窄迁移入口为 `-run=BattleHUDAssets -VehiclePresentationUpgrade`，附加 `-Inspect` 只读检查、`-Validate` 冷加载编译验证。迁移保存前比较原业务图、绑定、动画和既有布局；仅保存车辆按钮、成员模式、HUD 与新图标纹理，备份位于 `Saved/BattleVehiclePresentation/Backup/20261004-054800-7FB3B2FA4F68D9B09836178FA7E18A27/`。C++ 构建、冷验证及运行日志分别为 `Saved/OperationsPanelUpgrade/VehiclePresentationBuild.log`、`VehicleAuthoredBuild.log`、`VehiclePresentationColdVerify.log`、`VehicleMultiSelectRuntime.log`、`VehicleAuthoredRuntime.log`。冷验证成功；编辑器命令进程仍因既有 EasyHouseBuilder Toolset 注册错误返回 1，该错误与本次资产无关。

同日车辆面板移动与固定引用更新：保留用户将 `VehiclePanel` 放入 `MemberScrollBox` 的布局，升级为必需 `BindWidget`，去除运行时控件树查找。`AuthoredVehicle` 验证固定引用、按钮开关、原父容器、人员互斥及重新挂载；`RuntimeInteraction`、`Roster.RuntimeFlow` 回归均通过。构建与冷加载编译也通过，日志为 `Saved/OperationsPanelUpgrade/VehicleFixedBindingBuild.log`、`VehicleFixedBindingColdVerify.log`、`VehicleFixedBindingRuntime.log`。本轮没有保存蓝图资产，成员模式、车辆面板和 HUD 的文件哈希与修改前一致。只读 Inspect/Validate 支持新层级；旧 Apply 遇到已移动面板会在修改前拒绝，避免覆盖用户布局。
