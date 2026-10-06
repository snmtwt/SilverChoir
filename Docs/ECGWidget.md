# 心电图控件

资源：`/Game/System/UIBasic/WBP_ECG`。父类 `UECGWidget`，Designer 内含一个 `ECGView`（H5 UI View），加载 `coui://uiresources/ECG/ecg.html`。它是独立、可复用的显示控件，可以直接放进其他 Widget Blueprint 并由父容器设置尺寸；建议人员卡使用 128×48 至 320×96。设计器默认预览尺寸为 320×96。

## 蓝图参数

| 参数 | 含义 | 默认值 |
| --- | --- | --- |
| 颜色 / WaveColor | 波形、网格及扫描装饰的颜色，支持 Alpha | 青色 |
| 心率 / HeartRateBPM | 每分钟心跳次数，范围 0–300 | 72 |
| 心跳强度 / HeartbeatIntensity | 波峰高度，范围 0–2；0 立即清除旧波峰并显示平线 | 1 |
| 动画种子 / AnimationSeed | 0 为每个控件自动生成独立种子；非 0 使用指定种子，复现起始波形相位和扫描位置 | 0 |

三项参数可以在控件 Details、Create Widget 的输入引脚，以及运行时蓝图 Setter 中设置。也可调用“设置心电图参数”（`SetECGParameters`）一次更新三项。设置心率为 0 同样没有心跳，但扫描刷新继续运行。颜色/参数更新不会重新加载网页。

动画种子在“心电图 / 动画”中设置，或调用“设置心电图动画种子”（`SetAnimationSeed`）。保持默认 0 即可让同一帧创建的多个人员面板自动错开波峰与扫描头，不需要修改卡片蓝图。自动种子在该控件实例存活期间保持不变；重复设置同一值、刷新生命状态、颜色或其他样式都不会重新播种。切换到不同的固定种子会重新建立扫描历史；从固定种子切回 0 会为当前实例生成新的自动种子。相同的固定种子、心率、扫描周期与尺寸可复现相同的起始轨迹。

“心电图 / 样式”下还可调整 `SweepSeconds`（一轮扫描的秒数）、`GlowStrength`、`bShowGrid`、`bShowBackground`。在运行时直接修改这些样式变量后，调用“刷新心电图样式”（`RefreshECG`）。关闭背景可让心电图叠加到现有人员面板中。

例如：正常用 72 / 1；快速心跳用 144 / 1；微弱心跳用 48 / 0.25；死亡将强度设为 0。它展示游戏状态，不自行判断人员死亡，也不会修改单位数据。

## 动画和实现

H5 的 `ecg.js` 定义 P-QRS-T 形态、相位、扫描历史、擦除间隙及余辉样式。种子独立决定心跳与扫描的初始偏移；心率影响相位推进速度，强度影响振幅，种子不会改变两者。进入死亡平线时历史峰值立即清零；恢复后新波形随扫描写入。每个控件拥有独立的动画状态，使用 UI 实时时间，不受游戏时间倍率影响。

C++ 的 `UECGWidget` 负责蓝图参数校验、颜色转换、初始数据与页面事件、Widget 生命周期；H5UI 插件负责 Canvas 路径光栅化、透明度、渐变和纹理提交。动态纹理随位图尺寸复用，不每帧创建 UObject；控件移除时关闭页面并释放动画回调。基础控件不接收鼠标和键盘输入。

H5 文件位于 `Content/UI/ECG`，现有 H5UI Build.cs 已将整个 `Content/UI` 配置为打包时的 NonUFS 资源。完整打包仍需在项目正常发布流程中验证。

## 验证入口

- `H5UIPlugin.Canvas.BitmapAndDOM`：位图保留、局部透明擦除、透明度叠加、渐变、曲线、缩放、状态保存恢复、同尺寸重置、多个实例与尺寸上限。
- `H5UIPlugin.Canvas.AnimationFrames`：递归 rAF 每次更新最多执行一次，以及页面关闭/重新加载的隔离。
- `H5UIPlugin.Canvas.ECGParameters`：初始数据、心率与相位、运行时事件、立即死亡平线。
- `SilverChoir.UI.ECG.RuntimeFlow`：实际 UE 进程中的四个独立控件、蓝图接口、动画与截图。
- `SilverChoir.UI.ECG.SeededInstances`：六张实际人员卡片的默认独立种子、动画错位、刷新连续性、固定种子复现及死亡平线。
- `node Scripts/verify_ecg_seed.cjs`：H5 动画种子的纯 JavaScript 回归。

原生 Canvas 的详细支持范围见 `Plugins/H5UIPlugin/Docs/HTML_CSS_COMPATIBILITY.md`。本次没有替换既有战斗 HUD 的旧心电图，便于先独立调整效果后再接入。

## 本机验证记录（2026-10-03）

编辑器 Development 编译通过，Canvas 三项测试、既有 CSS 动画、HTML/CSS 兼容和外部纹理生命周期回归通过。实际游戏进程中验证了四个独立控件、实时参数更新和立即平线，并检查了 1920×1080 截图。

截图位于 `Saved/Screenshots/ECG/01_ECG_States.png` 和 `02_ECG_LiveUpdate.png`。运行日志位于 `Saved/OperationsPanelUpgrade/ECGFinalRuntime.log`，回归日志位于 `ECGRegression.log`。

动画种子更新后，Development 编译通过；`verify_ecg_seed.cjs` 的 9 组回归及 `SilverChoir.UI.ECG.SeededInstances` 实际运行测试通过。六张默认人员卡片无需额外蓝图接线即可获得独立动画，同时验证了参数刷新连续性、固定种子复现与死亡平线。截图为 `Saved/Screenshots/ECG/03_ECG_SeededCards.png`，日志为 `Saved/OperationsPanelUpgrade/ECGSeedRuntime.log`。

当前 Canvas 为 CPU 绘制后上传纹理，适合小尺寸 HUD。此机器上的单帧抽样：160×56 单实例脚本及原生绘制约 1.3–1.6 ms、提交约 0.05 ms；560×112 约 4.0–4.7 ms。这不是完整性能基准，不能据此保证大量实例的帧率。后续接入六人面板时建议使用人员卡实际的小尺寸并统一评估帧预算。
