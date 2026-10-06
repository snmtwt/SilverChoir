# 战术图标按钮

蓝图继续使用 `/Game/System/UIBasic/WBP_RaisedImageButton`，原生类为 `URaisedImageButtonWidget`（编辑器显示名：战术图标按钮）。保留现有名称，已有界面实例自动使用新样式。

## 外观和交互

- 正常：透明底、蓝灰色四角角标、浅蓝图标。
- 悬浮：角标与图标提亮；一条细扫描线带短拖尾，自下向上循环，默认 1.8 秒一轮。鼠标离开、控件隐藏或禁用后复位。
- 按下：输入开始时立即播放继承的 PressSound；经过两个引擎帧后角标内收 2、图标缩至 90%。松开恢复。快速点击仍保留至少一帧按下反馈，然后只触发一次 OnClicked。
- 选中：淡青色底与底部短亮线持续保留；再次按下时保留选中标记并显示内收反馈。点击本身不自动切换选中状态，业务蓝图通过 SetSelected 控制。
- 默认外部尺寸仍为 44×44、图标范围 20×20。按压只改变渲染缩放，不改变布局或点击范围。图片默认保持原比例。

## 蓝图配置

| 位置 / 节点 | 用途 |
| --- | --- |
| 按钮 → 内容 → 图标 | 设置图片 Brush；建议使用白色透明底 PNG，Brush 的 Tint 保持白色 |
| 设置按钮图片 / SetButtonImage | 运行时替换纹理；传入空值清除 |
| 按钮 → 图标颜色 | 分别设置正常、悬浮、选中、按下颜色 |
| 设置图标状态颜色 / SetIconTintColors | 运行时一次更新四种状态颜色 |
| 按钮 → 样式 → 强调颜色 / 边框颜色 | 高亮角标、扫描线、选中底色与标记，以及正常角标颜色 |
| 按钮 → 战术样式 | 角标长度、按下内收量、按下图标比例、选中底色透明度 |
| 按钮 → 悬浮扫描 | 开关、扫描周期（秒）、扫描亮度 |
| SetSelected | 设置持续选中状态 |
| 按钮 → 声音 | 复用项目的 HoverSound、PressSound |
| OnClicked | 连接业务逻辑 |
| OnPressStarted | 输入开始时的附加通知；已配置 PressSound 时不要再重复播放同一音效 |

图标颜色优先级：按下 > 悬浮／键盘焦点 > 选中 > 正常。禁用时变暗。

设计器的“预览状态”提供正常、悬浮、按下、禁用、选中、选中并按下；仅影响 Designer。扫描线在 Designer 中显示静态中段位置，在游戏运行时循环。

原有 Elevation、PressedDepth、CornerRadius 字段仅保留旧资产兼容，不再参与平面按钮绘制。

## 资源和绑定

- `/Game/System/UIBasic/Textures/T_ArrowUp`
- `/Game/System/UIBasic/Textures/T_ArrowDown`

原图在 `Content/UI/UIBasic/Icons/`。PNG 为 512×512，所有可见像素 RGB 都是纯白，抗锯齿通过 Alpha 表示。对应 SVG 是可编辑矢量源，上下图标互为 180 度旋转。图标制作记录见同目录 `Arrows.Generation.md`。

Designer 树保持 `ButtonSize`（SizeBox）→ `FaceContent`（Overlay）→ 居中的 `IconSizeBox`（SizeBox）→ `IconImage`（Image）。这些名字用于原生绑定，视觉子项不拦截输入，运行中不逐帧按名称查找。

## 验证入口

`-run=RaisedImageButtonAssets -Upgrade`：备份已有纹理和按钮包，原位重导入两张白色箭头，验证纯白像素和直接使用者编译；只保存两张纹理。按钮蓝图、业务图表、布局和实例设置保持现状。

`-run=RaisedImageButtonAssets -Validate`：冷加载验证，不保存。

`SilverChoir.UI.RaisedImageButton.RuntimeInput`：真实蓝图、Slate 输入与固定命中区域，检查同步按下事件、两帧延迟、选中、颜色、扫描、快速点击和取消。

`SilverChoir.UI.RaisedImageButton.StatePreview`：真实 UE 控件在 44／56／96 尺寸、六种状态的截图，包含不同扫描位置。输出 `Saved/Screenshots/RaisedImageButton/States.png` 和 `StatesScanLater.png`。

## 2026-10-05 验证结果

- C++ 构建成功（Saved/RaisedImageButton/DigitalBuild.log）。
- Upgrade 与冷加载 Validate 报告均为 PASS，纯白像素检查通过；WBP_RaisedImageButton 和直接使用者 WBP_人员卡片均为 0 错误、0 警告。
- 真实运行测试 RuntimeInput、StatePreview 均成功，报告 DigitalRuntime/index.json：2 成功、0 失败、0 警告。已查看两张实际截图，扫描线在第二张中向上移动。
- 基础按钮回归 ButtonWidgetInputAndContent、ButtonIdleFeedback500、DelayedButtonTiming 全部通过。
- 音效验证范围：确认已有 PressSound 资产已配置，输入路径即时调用 PlaySound2D；自动测试验证输入开始事件与按下同帧、按下视觉至少延迟两帧。测试使用无声模式，不声称检测了实际扬声器输出。
- UE 启动阶段仍有项目原有 ToolsetRegistry / EasyHouseBuilder 注册日志；这使资产命令进程返回 1，但专项 Upgrade/Validate 明确为 PASS。实际运行及基础回归进程返回 0。
