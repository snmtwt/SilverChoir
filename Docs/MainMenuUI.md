# 主菜单：C++ 基类、UMG 蓝图与原生 H5 背景

## 直接编辑的资源

全部位于 `/Game/System/Map/MainMenu`，现有 `MainMenu` 关卡已设置蓝图 GameMode。

| 资源 | 父类 / 职责 |
| --- | --- |
| `WBP_MainMenu` | `MainMenuWidget`；在 Designer 编辑标题、装饰文字、背景位置和菜单布局 |
| `WBP_MenuButton` | `BasicButtonWidget`；在 Designer 编辑图标、主副标题、编号和箭头的排布 |
| `BP_MainMenuPlayerController` | `MainMenuPlayerController`；默认界面为 `WBP_MainMenu`，覆写 `HandleMenuAction` 接业务 |
| `BP_MainMenuGameMode` | `MainMenuGameMode`；使用上述控制器，不生成角色或 HUD |

`MainMenuWidget` 是抽象 C++ 基类，只定义控件绑定、生命周期和事件，不在运行时创建菜单布局。
`DefaultEngine.ini` 的 MainMenu 地图名前缀也已指向蓝图 GameMode。
打开 `WBP_MainMenu` 的 Designer 即可修改布局；常规修改不需要运行生成工具或重新编译 C++。
背景独立填满根 Overlay，不再放进固定 16:9 设计区域，没有左右留空或上下黑边。
前景使用 SafeZone、Canvas 锚点和项目 DPI 缩放：左侧标题、中央动态雷达、右侧菜单，上下信息贴边。
16:9、16:10 和 4:3 使用可用画幅布局；超过 16:9 时，主体构图区宽度限制为高度的 16/9 并居中，
防止 21:9、32:9 把菜单和圆环拉得过远。背景及顶部、底部信息仍覆盖整个视口。
雷达中心始终在画幅 50%、50%；通常直径为 46vh，在 3:2 及更窄画幅（包含 4:3）改为 27vw，为放大后的标题与右侧按钮留出空间。
雷达使用距离环、36 个周向刻度、分段圆弧、旋转扫描线/尾弧和三个闪烁信号标记；保持原生 H5 渲染，无 JS 或 CEF 依赖。

### 当前布局的调整位置

- `WBP_MainMenu / Foreground / ContentFrame / MenuComposition`：左侧标题组；锚点为构图区宽度 17.5%、高度 50%，水平偏移 -176，容器 430×395，垂直居中。1920 逻辑宽度时比原位置右移 64；4:3 下收小移动量，以保留雷达间距。位置脚本为 `Scripts/move_main_menu_title.py`。
- `ContentFrame`：C++ 仅在视口尺寸变化时更新这个容器的宽高，其内部控件排布保留在 Designer 中。
- `ContentFrame / MenuButtons`：右侧按钮组，锚点 90%、50%，右对齐，水平偏移 -34；按钮宽 300、最小高 48、间隔 8，主文字 14 pt。主终端标题、提示和横线同步对齐；对应用户红框位置，在 1920×1080 下比前一版再左移 90 个布局单位。
- `TitleSilver`：90 pt；`TitleChoir`：102 pt 粗体（此前交付的 60/68 pt 的 1.5 倍）；中文标题 15 pt。保留原银白和蓝色，主界面装饰文字多数为 8–10 pt。
- `SignalCoreText`、`SignalCoreCaption`：构图区内的中心锚点为 50%、50%，与背景 `.signal-core` 一致；`SignalTelemetry` 位于 50%、78%。
- `BackgroundView`：根 Overlay 的全屏背景，独立于前景尺寸。

尺寸使用 UMG 逻辑单位，最终由项目 DPI 规则缩放。调整标题和按钮直接使用 Designer；
若移动轨道中心，同时修改两个原生中心文字的 Canvas 锚点及 CSS 的 left/top。

## C++ 与蓝图的边界

地图基类位于 `Source/SilverChoir/Public/Map/MainMenu`，基础按钮位于 `Public/UIBasic`。
实现位于 `Private` 对应目录。C++ 负责输入、延迟时序、控件契约和事件；Designer 负责排布。
`WBP_MainMenu` 必须保留以下同名控件（BindWidget，缺失或类型错误会产生蓝图编译错误）：

| 名称 | 类型 |
| --- | --- |
| `BackgroundView` | H5 UI View |
| `NewGameButton`、`LoadGameButton`、`SettingsButton`、`QuitButton` | BasicButtonWidget 子类，当前为 WBP_MenuButton |

标题、状态文字及装饰线可自由增加和调整。菜单通过 `OnMenuActionRequested` 将操作传给 Controller。
在 `BP_MainMenuPlayerController` 覆写 `HandleMenuAction` 接新游戏、存档和设置页面。
目前只有 Quit 默认调用 UE 的退出功能；其余三个是业务接入点，尚未实现玩法关卡切换、存档页和设置页。
Controller 仅为本地玩家创建界面，显示鼠标，设置 UI Only 输入并聚焦首个按钮。
`HideMainMenu` 会取消待触发点击、移除菜单并恢复游戏输入。

## 基础按钮

按钮不使用 UButton 或 SButton。输入由 UUserWidget 处理，背景和边框由 Slate 绘制。

- `ContentMode`：图标＋文字、仅文字、仅图标。
- `ButtonText`、`ButtonSubtitle`、`ButtonIndex`：主标题、副标题和编号，支持 FText 本地化。
- `IconBrush`、`IconSize`：图标资源和尺寸。
- `Font`、`MinimumSize`、`BorderThickness` 及各颜色：在蓝图默认值或实例 Details 调整。
- `OnClicked`、`OnPressStarted`、`OnHovered`、`OnUnhovered`：事件分发器。
- `PressSound`：可选音效；菜单按钮已绑定 `/Game/System/Map/MainMenu/Audio/UI_MenuPress_Terminal`。
- `HoverSound`：可选音效；菜单按钮已绑定同目录 `UI_MenuHover_Electronic`，鼠标进入时播放一次，停留不重复播放，禁用/隐藏时不播放。
- `SetSelected` 保持高亮；`CancelPendingClick` 取消操作；禁用和隐藏也会取消。

按钮蓝图的可选绑定为 `ButtonSize`（SizeBox）、`IconSizeBox`（SizeBox）、`IconImage`（Image）、
`Label`、`DetailLabel`、`IndexLabel`（TextBlock）。按需保留，文字和图标可以独立绑定。
可以自由调整位置，C++ 根据按钮属性同步内容。没有 Designer 树的基础按钮仍有简单图文布局。
主菜单专用的箭头和副标题等排布由 `WBP_MenuButton` 完成。

点击流程：按下帧 N 立即播放可选声音并发出 OnPressStarted；N+2 显示按下效果；
效果至少保留一帧，并在按钮内有效松开后发出一次 OnClicked。
快速点击同样保留反馈，长按等待松开。重复按下不堆积；越界松开、失焦、丢失捕获、隐藏和销毁会取消。
Enter、Space、手柄确认键使用同一流程。

菜单悬浮音及保留的旧点击音来自 EZduzziteh 的 [UI Click Sounds (Sci Fi)](https://opengameart.org/content/ui-click-sounds-sci-fi)（CC0）。
悬浮使用 `sfx_ui_click_8` 的 120 ms 短版本，音量 0.16；当前按下音为项目内原创合成的 240 ms 电子确认音，音量 0.50，由低频起音、上行 FM 音和轻微高频泛音组成，脚本为 `Scripts/prepare_terminal_press.py`，未使用第三方音频采样。
两者移除前导静音、添加淡入淡出并转换为单声道 48 kHz / 16-bit PCM WAV；来源与原始文件保存在 `SourceAssets/Audio/UI/SciFiOriginals`。
Sound Group 为 UI，关闭循环，并使用 Force Inline 加载。原 Kenney 点击声 `UI_MenuPress` 和前一版较重的 `UI_MenuPress_Electronic` 保留供替换，不再绑定菜单。
声音在按下当帧调用 PlaySound2D，不等到松开或 OnClicked；实际可听延迟取决于音频设备。
`Scripts/import_menu_audio.py` 仅导入声音并修改按钮默认值、四个 Designer 按钮实例的 PressSound/HoverSound，保存前自动备份，
不会重建布局；加 `-MenuAudioVerify` 可在新进程中只读校验已保存的引用。
加 `-MenuPressOnly` 可只替换/校验点击音，保留当前悬浮音选择。

四边使用相同厚度，结合 UMG/DPI/父级缩放对齐物理像素，至少保留 1 像素。
左侧强调条独立绘制。该像素保证针对通常的轴对齐布局；旋转控件不能保持轴向整数像素边缘。

## H5 插件与性能选择

CEF 模块保留，LoadingPhase 为 None，按需加载。新属性 `Enable CEF IFrames` 默认关闭。
关闭时不创建浏览器画布、不进行每帧 iframe 扫描，也不创建浏览器实例。
今后使用插件已有的 iframe 能力时，在 H5 UI View 的 Browser 分类开启该属性，
或调用 `Set CEF IFrames Enabled`；真正遇到可见 iframe 后才加载 CEF。
这是现有 iframe 集成的开关，不会把整个 H5 UI View 自动改成浏览器渲染。

本主菜单在 C++ 中明确关闭 CEF、JavaScript 和输入，背景设为 HitTestInvisible。
全部文字和按钮均由 UE 原生 UMG 实现；H5 只负责装饰背景。
`BackgroundFrameRate` 默认 60，可在主菜单蓝图类默认值中调整。未做与 CEF 的性能基准比较。

背景文件为 `Content/UI/MainMenu/background.html` 和 `background.css`。
`BackgroundURL` 默认 `coui://uiresources/MainMenu/background.html`。
之前动画不动是插件把 CSS 的 linear / reverse 解析成了动画名称，并非关闭 JavaScript 导致。
现在插件直接识别 linear、normal、reverse、alternate-reverse、running，及动画和 transition 的 s/ms 时间单位。
主菜单使用标准写法 `animation: orbit 24s linear infinite reverse`，无需复制反向关键帧。
扫描线、反向轨道、闪烁标记和边缘括号由 H5 绘制；标题、菜单编号和状态文字通过 UMG 实现。

原生渲染器按需求扩展，不保证任意浏览器 HTML/CSS 原样运行。完整边界见
`Plugins/H5UIPlugin/Docs/HTML_CSS_COMPATIBILITY.md`。
CSS Grid、WebGL、完整滤镜和浏览器平台 API 不在主菜单原生实现范围内。H5UI 后续已增加 Canvas 2D 基础子集，能力边界见上述兼容文档的 Native Canvas 2D 一节。
构建规则会将 Content/UI 作为 NonUFS 运行资源纳入暂存；尚未执行正式 Cook/打包验证。

## 资源生成与验证

`SilverChoirEditor` 是独立 Editor 模块，游戏 Runtime 不依赖它。
`-run=MainMenuAssets` 用于首次生成四个蓝图；资源已存在时会拒绝覆盖，以保护 Designer 修改。
`-run=MainMenuAssets -Verify` 重新加载资源，编译 Widget 蓝图并校验绑定及类引用。
`-Redesign` 是本次布局迁移入口，会备份后替换两个 Widget 的 Designer 树，保留 GameMode/Controller；
日常编辑不要重复运行，以免覆盖 Designer 调整。
`-FixFonts` 是旧生成资源的字体引用维护入口，常规编辑不需要使用。
覆盖保存前的备份位于 `Saved/MainMenuAssetBackups`。

编译目标为 `SilverChoirEditor Win64 Development`，自动化筛选为：
`SilverChoir.UI+H5UIPlugin.Runtime.StandardCssAnimations+H5UIPlugin.Runtime.HtmlCssCompatibility`。
覆盖点击时序及取消、内容切换、像素边框、无输入/JS 持续动画、标准 CSS 方向及时间单位和 CEF 默认关闭配置。
测试报告位于 `Saved/Automation/MenuBlueprint/index.json`，运行截图位于 `Saved/Screenshots/MenuQA`。
`SilverChoir.CaptureMenu` 仅在带 -game 的测试进程中截取两帧并退出，不会关闭交互编辑器。

本次全屏重设计的四项 `SilverChoir.UI` 回归测试通过，报告为 `Saved/Automation/MenuResponsive/index.json`。
背景测试复用同一个已加载文档，依次切换 1920×1080、1920×1200、3440×1440、3840×1080、2132×728、1024×768、720×1280、1280×720，
校验背景满屏、轨道保持正圆、竖屏媒体查询生效以及无 JavaScript 的动画持续运行。
最终宽高比构图截图以 `Aspect_` 开头，位于 `Saved/Screenshots/MenuQA`。
已通过 UE 离屏运行检查 1920×1080（16:9）、1920×1200（16:10）、1600×1200（4:3）、
3440×1440（超宽屏）、3840×1080（32:9）和 2132×728，菜单和圆环完整显示，背景覆盖整个画幅。
最新预览保存在 `Docs/Images/MainMenu.png` 及同目录 `MainMenu_16x10.png`、`MainMenu_4x3.png`、
`MainMenu_21x9.png`、`MainMenu_32x9.png`。
最终编辑器编译成功；资源保存后再次运行 `-Verify`，四个蓝图编译、控件绑定和类引用验证均通过（0 错误、0 警告）。

最新三栏布局由 `Scripts/layout_three_column_menu.py` 移动现有 Designer 控件实现，自动备份且保留所有按钮的悬浮/点击声音引用。
三栏更新后的编辑器编译和四项 `SilverChoir.UI` 测试通过，报告为 `Saved/Automation/MenuThreeColumn/index.json`。
已检查 1920×1080、1920×1200、1600×1200、3440×1440 和 3840×1080 的 UE 实际渲染；
截图位于 `Saved/Screenshots/MenuQA/ThreeColumn_*`，`Docs/Images` 中的五张预览同步更新。

标题视觉调整保存在 Designer 中，脚本为 `Scripts/style_main_menu_title.py`；放大两行英文并重排中文名/副标题间距。
这是蓝图视觉编辑，C++ 首次生成工具保留初始样式；重建布局工具不用于日常 Designer 编辑。

### 标题扫光

`TitleSilver`、`TitleChoir` 的 Font Material 分别绑定 `Materials/MI_TitleSheen_Silver` 和 `MI_TitleSheen_Choir`。
原生 UI 字体材质 `M_TitleSheen` 只对当前字体颜色做局部提亮，基础配色、字号和布局不变，光带限制在字形内。
默认每 5 秒扫过一次，持续 1.6 秒；在材质实例中可调整 `Interval`、`Duration`、`Gain`（设 0 关闭）与 `RowOffset`。
由材质 Time 驱动，不需要每帧蓝图逻辑、额外纹理或浏览器；编辑脚本为 `Scripts/add_title_sheen.py`。
已通过保存后资源校验、编辑器编译及 UE 离屏连续帧检查，动画预览为 `Docs/Images/MainMenuSheen.gif`。
截图命令可加 `-MenuCaptureSequence` 捕获 60 帧动画；不改变正常游戏逻辑。

### 菜单退场与加载页

在 `WBP_MainMenu` 或持有菜单引用的蓝图调用 `PlayMenuExit(SelectedAction)`。
返回 false 表示已有退场、已退场或控件绑定无效，不会重复排队。
从下往上跳过所选按钮，最后再移出所选按钮；例如 LoadGame 的顺序是 Quit、Settings、NewGame、LoadGame。
启动帧为 N、N+2、N+4、N+6，N 是首次处理动画的引擎帧；每个按钮默认滑动 0.32 秒并淡出。
两个帧间隔随实际帧率改变，不能按固定毫秒理解。`ExitSlideDuration` 可调单按钮时长。
动画期间禁用按钮输入，原始位移、透明度和可见性由 `ResetMenuExit` 恢复；移除界面也会清理状态。
绑定 `OnMenuExitFinished(Action)` 执行后续逻辑，不要调用退场后立刻 OpenLevel。

“开始行动”已由原生控制器接好退场 → MapTransitionSystem.SwitchMap → GameMainMap → 基地就绪。
主菜单控制器蓝图的 `NewGameMap` 与 `LoadingWidgetClass` 可更换；覆盖 HandleMenuAction 时需要自行调用退场，或调用父实现。
继续任务与系统设置没有虚构业务；可在蓝图处理对应动作，调用相同退场接口并监听完成事件。

加载页面 C++：`Public/UIBasic/MenuLoadingWidget.h` / `Private/UIBasic/MenuLoadingWidget.cpp`。
子蓝图：`/Game/System/UIBasic/WBP_MenuLoading`，继承 MTS_MapLoadingWidget 的项目子类。
Designer 复制当前主菜单的标题、扫光、H5 雷达和自适应布局，移除按钮；右侧为部署标题、状态、百分比和细进度条。
必须绑定 `BackgroundView`、`ContentFrame`、`LoadingTitle`、`LoadingStatus`、`LoadingPercent`、`LoadingProgress`。
这些控件的布局/字体/样式都可在 Designer 编辑，运行时 C++ 更新进度内容。
进度来自 MTS 的阶段状态，不是磁盘字节百分比。普通 OpenLevel 在阻塞游戏线程时，UMG/H5 动画也可能短暂停顿；本实现没有引入独立 MoviePlayer 加载线程。

创建工具：`-run=MenuLoadingAssets`（已有加载蓝图保留布局，控制器引用会更新且先备份）。
2026-09-22：编译通过，加载蓝图保存检查 0 错误/0 警告；`SilverChoir.UI` 5 项通过，真实 `SilverChoir.MenuTravel.StartAction` 1 项通过。
报告：`Saved/Automation/MenuExit`、`Saved/Automation/MenuTravel`。预览：`Docs/Images/MenuLoading.png`。
