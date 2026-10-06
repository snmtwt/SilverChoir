# 基地区域块

将 `/Game/System/Map/BaseMap/BP_RegionBlock` 拖入基地地图。C++ 基类位于 `Public/Map/BaseMap/RegionBlock.h`。

## 创建形状

- 选中 Actor，在细节面板的“区域 | 形状”设置 RectangleWidth（厘米），点击“生成矩形曲线与体积”。RectangleLength 为 0 时生成正方形；填写长度可生成长方形。
- 或编辑 Boundary 样条控制点，保持 Closed Loop 开启，然后点击“根据闭合曲线生成体积”。支持凹多边形和曲线控制点，不支持自相交或带孔轮廓。
- Height 控制体积高度。轮廓投影到局部 XY 平面，底面为局部 Z=0，高度沿局部 +Z 延伸；通过 Actor 的位置放置在地面上。
- bAutoRebuild 默认开启，编辑曲线或属性时自动更新；关闭后可通过按钮手动生成。
- CurveSampleSpacing 控制曲线精度。直线段仅保留端点；曲线采样最多 512 点，复杂轮廓可增大间距。
- 无效轮廓会清空旧网格并显示 LastBuildError，避免旧碰撞仍然存在。

## 显示与交互

RegionName 是顶部名称；名称 UI 平铺在顶面朝上，不会跟随相机转动。可设置 NameDrawSize、NameScale 和 NameTopOffset。
对凹形区域，名称锚点会保持在多边形内部；较长文字可能超出狭窄区域，可调整文字或缩放。

原生 `URegionNameWidget` 默认按需重绘：初次显示、改名、悬浮、点击反馈及恢复、重新显示时绘制一次，完成后停止名称组件 Tick，保留已生成的文字纹理。隐藏时停止名称组件 Tick；隐藏期间改名不会绘制，重新显示时会更新为最新名称。“名称持续更新” (`bContinuouslyUpdateName`) 可让原生名称继续逐帧更新。自定义名称类（包括 `WBP_RegionName`）默认持续更新，保留蓝图动画及自行更新内容的行为，不要求打开此开关；隐藏和重新显示仍会停用、恢复其组件 Tick。

默认 M_RegionBlock 为半透明、自发光菲涅尔材质。NormalColor 与 HighlightColor 配置普通/悬浮颜色。
悬浮同时高亮网格与名称。鼠标左键点击产生 0.15 秒亮度反馈，并广播 OnRegionClicked。
可绑定 OnRegionHoverChanged 与 OnRegionClicked，在蓝图中打开区域面板、选择建筑区域等。

调用 ShowRegion / HideRegion / SetRegionShown 控制运行时可见性。
隐藏会关闭体积和名称显示、关闭查询碰撞、清除悬浮和点击反馈，隐藏状态不接受交互。
bInitiallyShown 控制游戏初始状态。隐藏不会卸载 Actor 或停止你额外添加的其他业务逻辑。

网格仅阻挡 Visibility 查询，不阻挡 Pawn 或物理运动。GameMainMapPlayerController 已开启鼠标悬浮和点击事件。
若换用其他 PlayerController，需要开启 ShowMouseCursor、EnableMouseOverEvents、EnableClickEvents，并使用 Visibility 点击检测通道。
区域不是实体墙，也不是 NavMesh 障碍。重叠区域由鼠标射线首先命中的体积接收交互。

## 蓝图扩展

- 在 BP_RegionBlock 子蓝图的“重写”菜单选择“区域被点击 / ReceiveRegionClicked”。收到有效左键点击时调用，可在此实现打开面板或按 GameplayTag 切换功能场景。默认高亮脉冲会先执行，原有 OnRegionClicked 分发器继续广播；隐藏区域不会触发此事件。

- `/Game/System/Map/BaseMap/UI/WBP_RegionName`：可在 Designer 添加名为 RegionNameText 的 TextBlock；不提供布局时使用 C++ 文本布局。
- RegionNameWidget 提供 RegionName、LabelColor、bIsHighlighted 和 OnRegionLabelUpdated 事件，便于自定义动效。
- 如替换材质，请保留 RegionColor（Vector）、GlowStrength（Scalar）、FillOpacity（Scalar）参数。
- 默认不创建任何区域实例或修改基地关卡，按游戏设计自行放置 BP_RegionBlock。

## 验证

SilverChoir.Regions.Geometry 检查矩形、凹多边形、反向轮廓、顶点去重、自相交及零面积拒绝。
SilverChoir.Regions.Runtime 在游戏世界检查体积射线命中、凹口无碰撞、隐藏禁用碰撞和交互、名称朝向，以及名称 RenderTarget 的实际像素：初次文字、悬浮/点击高亮与恢复、改名、隐藏期间不重绘、重新显示更新最新文字。还验证静态名称休眠，以及原生持续更新开关和自定义蓝图名称的连续绘制。
运行验证截图输出到 Saved/Screenshots/RegionBlock。

按需重绘改动的构建和运行验证由后续统一执行；上述测试范围不是本轮已通过记录。

## 网格实现

使用 EasyHouseBuilder 的 FEHBPlanarSurfaceGeometryBuilder::BuildSlabSurface 生成体积，UEHBGeneratedMeshComponent 负责渲染和查询碰撞。有效重建直接更新 section，保留插件对相同数据的跳过优化；无效输入才清空旧网格。项目不再因区域块依赖 ProceduralMeshComponent。

名称控件默认在顶面内旋转 180 度（相对旋转 Pitch=90、Yaw=180、Roll=0），仍朝局部 +Z。NameMaterial 默认使用 M_3DText：透明、自发光、补偿自动曝光，保留 SlateUI 的文字颜色和透明度。此材质用于 NameWidget 的材质槽，不用于 TextBlock 的字体材质槽。

名称材质维护工具：RegionTextMaterial commandlet 从引擎 Widget3DPassThrough 复制完整节点连接到 M_3DText，再补偿曝光；执行前自动备份至 Saved/RegionTextBackups。

默认名称文字使用 Bold 和 1 像素同色描边，增强中文回退字体的笔画。若在 WBP_RegionName 中自建 RegionNameText，请在 Designer 的 Font 中选择粗体并设置 Outline Size；C++ 保留此自定义字体样式。

M_3DText 的 TextBrightness 控制 HDR 自发光。区域块“区域|名称”中的“名称发光强度”默认 3，“名称高亮发光强度”默认 6，分别用于普通和悬浮/点击状态。周围光晕依赖摄像机或 Post Process Volume 的 Bloom，关闭 Bloom 时仍有明亮文字，但不会有扩散光晕。它不会自动照亮附近几何体。

## 基地区域鼠标检测（2026-09-27）

鼠标悬浮和点击统一通过 BaseRegion（ECC_GameTraceChannel2）查询通道检测。DefaultEngine.ini 将该通道的默认响应设为 Ignore；RegionBlock 在构造及 SetRegionShown 时为 VolumeMesh 设置 Block，兼容旧蓝图和关卡中的碰撞配置。隐藏区域仍通过 NoCollision 禁用查询。

GameMainMapPlayerController 在 PlayerTick 的父类鼠标检测执行前，根据 GameMainMapGameState.IsBaseMap 选择此通道。非基地地图恢复 DefaultClickTraceChannel。基地内需要参与此鼠标通道的新交互对象应显式响应 BaseRegion；普通建筑保持 Ignore。UMG 的正常按钮与输入拦截不受影响。

修复原因：从主菜单加载基地后，相机到四个区域块的射线先命中约 Z=550 的 StaticMeshActor 建筑部件，而不是区域块（区域顶部约 Z=380/410）。Visibility 通道同时检测建筑与区域，导致区域悬浮和点击均不触发。独立查询通道避免改变建筑的物理碰撞。

验证：完整编译通过；从主菜单自动进入基地的 PIE 检查中，四个区域均被 BaseRegion 射线命中，逐个隐藏后均不再被命中。日志：Saved/Logs/RegionRuntimeFixed.log（PROBE DONE）。原有 SilverChoir.Regions.ClickOverride 测试通过。
