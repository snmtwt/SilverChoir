# 游戏地图切换处理类

C++ 目录：`Source/SilverChoir/Public/SubSystem/GameMapTransitionSystem`，实现位于对应 Private 目录。

继承关系为 `UMTS_SubMapHandler → UGameMainMapSubMapHandler → UGameMapSubMapHandler`。原有主地图 Handler 的身份保持不变，并与新游戏地图 Handler 放在同一目录。`BP_SubMapHandler_T5` 改为继承 `UGameMapSubMapHandler`，保留地图配置，不再重复实现通用事件图。

## 地图流程

- 子地图加载完成后切换并等待 `OverviewSceneTag` 场景。
- 场景就绪后提交初始化完成；插件完成进度后激活 `MapType` 指定的地图。
- 激活战斗地图后，以 `SquadIds` 初始化战斗 UI。重访已加载地图时也会刷新重新创建的 UI。
- 加载界面与输入锁仍由 MapTransitionSystem 管理。失败与卸载会释放场景等待、清空生成器注册表和小队 ID；不自动返回基地。

原来的 `LoadRequest`、`LocalEntryTransform`、`OverviewSceneTag`、`SquadIds` 已变为继承的 C++ 属性，仍可在 Handler 蓝图默认值中配置。`LocalEntryTransform` 继续是子地图内的局部进入点，实际位置叠加插件记录的地图变换。

## 小队生成节点

1. 在目标子地图中放置小队生成器。
2. 获取该子地图的 Handler，转换为 **游戏地图子地图处理类**，调用 **注册小队生成器**，`SquadSpawner` 接生成器自身。注册可在生成器 BeginPlay 中进行。
3. 所有需要的生成器注册完成后，对 Handler 调用 **生成小队**，传入有序的小队 ID 数组。

注册顺序就是配对顺序：注册 `[生成器B, 生成器A]`，输入 `[小队1, 小队2]`，对应 B 生成小队1、A 生成小队2。多个 Actor 的 BeginPlay 先后顺序不应当用作固定部署约定；需要固定位置配对时，由关卡蓝图或其他业务蓝图按明确顺序注册。

**生成小队** 是显式部署节点，没有自动接入每次地图进入事件，因此重访地图不会无意重建已有单位。若覆盖 **子地图加载完成** 做自定义部署，在部署完成后调用父类事件继续默认进入流程；注册和部署必须先于提交初始化完成。

数量不同会返回 `false`，输出包含两边数量的 `OutError`，记录 Error 日志并显示红色屏幕提醒，整个请求不会开始生成。重复的小队 ID、无效成员、失效生成器、无效队形或 Pawn 类也会在生成前拒绝。

重复注册同一个生成器不会增加数量或更改位置。失效引用保留空槽位，避免悄悄把后续小队移动到其他出生点。**注销小队生成器** 是显式移除注册项；**获取已注册小队生成器** 按原顺序返回列表。

只接受属于该 Handler 对应子地图实例的生成器，避免基地和其他区域混用。卸载时只清空注册引用；关卡拥有的生成器及其单位由原有 Actor 生命周期清理。已卸载 Handler 不能再次执行注册或生成。

生成期间若蓝图业务回调导致地图卸载或后续生成器失效，会停止后续生成并报告失败，此前已成功生成的小队保留；本节点不提供跨多个生成器的整体回滚。

## 蓝图迁移

`BP_SubMapHandler_T5` 的进入流程、初始化战斗 UI、卸载清理和小队注册逻辑迁入原生类。作战指挥室页面保留进入业务连线；`BP_SquadSpawner` 使用通用原生 Handler 注册自身，补齐原来未连接的生成器引用。

原资产与配置/连线报告备份于 `Saved/SubMapHandlerLifecycle/NativeGameHandler/Backup`。迁移工具为 `SubMapLifecycleAssets -NativeGameHandler`，`-Inspect` 只读检查，`-Apply` 备份后修改，`-Validate` 重新加载验证。

## 验证（2026-10-04）

- `SilverChoirEditor Win64 Development` 编译成功，VS 工程文件已重新生成。
- `SilverChoir.GameMapHandler.SquadRegistry.RuntimeFlow` 通过，覆盖顺序配对、重复注册、数量不符与后续项无效时不修改原队伍、失效槽位、跨关卡拒绝、卸载和旧 Handler 拒绝操作。
- `SilverChoir.BaseUI.OperationsCommandRoom.SubMapEntry.RuntimeFlow` 通过，覆盖实际 T5 蓝图父类、真实地图生成器注册、暂停进入、基地保留、首次与重访 UI 名册初始化、相机进入位置、卸载与等待取消。
- 独立编辑器进程重新扫描 182 个蓝图并验证 3 个相关蓝图；不再需要节点迁移，旧引用数量为零，无蓝图编译错误。四项原地图配置保留。

日志位于 `Saved/OperationsPanelUpgrade/GameMapHandlerBuild.log`、`GameMapHandlerRegistry.log`、`GameMapHandlerT5Entry.log`、`GameMapHandlerValidate.log`。后台运行测试禁用了桌面鼠标引起的相机边缘移动，游戏配置没有修改。

迁移过程中删除旧函数时依赖蓝图曾产生临时编译报错，随后已重新绑定并保存；独立重新加载验证无此错误。编辑器启动仍有原有 EasyHouseBuilder ToolsetRegistry 注册错误，运行测试启动仍有既有 PythonTestRunner 注册错误，本次未修改这些插件。
