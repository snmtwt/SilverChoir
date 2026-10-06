# 车辆模板测试数据

本次资源用于验证车辆数据创建、小队选车与人数上限。表的行结构为 `FVehicleTemplate`，不保存车辆实例 ID 或运行时位置。

## 资产位置

- 数据表：`/Game/System/Data/Vehicles/DT_VehicleTemplates`
- 预览纹理目录：`/Game/System/Data/Vehicles/Images`
- 原始图片目录：`SourceArt/Vehicles`
- 图片由内置 `image_gen.imagegen` 生成；完整初始与最终宽屏修改提示词保存在 `SourceArt/Vehicles/generation-prompts.json`，未使用 API 回退。
- 导入与验证脚本：`Scripts/Editor/create_vehicle_templates.py`

| 表行名 | 名称 | 座位数（含驾驶席） | 预览图片 |
| --- | --- | ---: | --- |
| `SportsCar_2Seat` | 疾影跑车 | 2 | `T_Vehicle_SportsCar_2Seat` |
| `Sedan_4Seat` | 巡航轿车 | 4 | `T_Vehicle_Sedan_4Seat` |
| `SUV_6Seat` | 远征越野车 | 6 | `T_Vehicle_SUV_6Seat` |

每行的 `Profile.PreviewImage` 引用对应的 `Texture2D`。图片为约 **16:9 的透明 PNG**，当前原始尺寸均为 **1672 × 941**；脚本允许比例与 16:9 相差不超过 1%，保留实际宽高，不拉伸、不裁切、不补成正方形。

导入采用 UI 纹理组、`TC_EditorIcon` 压缩、保留 Alpha、sRGB、双线性过滤、`NoMipmaps`、`PowerOfTwoMode=None`，禁用纹理流送，不限制最大尺寸。非二次幂图片不需要为了生成 mip 而改变比例。在 UI 中显示时也应保持图片比例，例如通过 ScaleBox 的 ScaleToFit 使用。

当前三行的 `EntityData.VehiclePawnClass` 为空。项目中检查到的 `SM_BaseUtilityVehicle` 是静态网格资产，并非可直接生成的车辆 Pawn 子蓝图；后续制作 `AVehiclePawnBase` 子蓝图后可在表中指定。

## 暂定数值

以下均为测试占位值，不代表最终游戏平衡：

| 行 | 最大耐久 | 最大燃料 | 载货上限 kg | 速度（瓦片/游戏小时） | 每瓦片体力消耗 | 每瓦片燃料消耗 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| SportsCar_2Seat | 100 | 60 | 80 | 6 | 0 | 4 |
| Sedan_4Seat | 140 | 70 | 180 | 4 | 0 | 2.5 |
| SUV_6Seat | 180 | 100 | 350 | 3 | 0 | 4 |

`StrategicMovementData` 表达战略瓦片之间的移动参数，不能当作 Pawn 在地图内的速度。路线、地形时间倍率、实际燃料扣除与战略移动流程尚未由本次资源实现。

## 创建车辆数据

蓝图节点 **根据车辆模板表行创建车辆数据数组**：

1. `Table` 指向 `DT_VehicleTemplates`。
2. `RowNames` 填入要创建的明确行名，如 `SportsCar_2Seat`、`Sedan_4Seat`、`SUV_6Seat`。
3. `TileId` 填入这些车辆的当前战略瓦片 ID。
4. 检查返回成功后使用 `OutData`；每辆车获得独立有效 ID、来源行名，耐久与燃料初始化为模板上限。

同一行可重复传入以创建多辆独立车辆；空行名列表不会隐式读取整表。此工厂节点只创建数据，随后调用玩家单位函数库的 **加载玩家车辆数据数组** 登记；未登记车辆不能分配给小队。

需要读取整表时，直接调用新增的 **从模板表加载玩家车辆**：传入本表和非空 `TileId`，每行创建一辆并追加到玩家单位子系统，返回新车辆 ID 数组。表引脚为空使用项目设置中的默认 `DT_VehicleTemplates`。该节点适用于新游戏初始化或明确发放车辆；每次执行都会生成新 ID，不要在每次打开会议室时调用。读档使用 **加载玩家车辆数据数组** 保留原 ID。会议室调用 **加载当前瓦片车辆列表**，或沿用 **展示小队列表** 的瓦片位置，即可从子系统读取；选车后正式保存小队才生效。

## 执行与复验

在项目编辑器已关闭时，从 PowerShell 运行：

```powershell
& 'R:\EpicGame\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'S:\UE_WorkSpace\SilverChoir\SilverChoir.uproject' `
  -run=pythonscript `
  '-script=S:\UE_WorkSpace\SilverChoir\Scripts\Editor\create_vehicle_templates.py' `
  -unattended -nosplash -NullRHI
```

新进程只读复验使用相同命令，额外加 `-VehicleTemplatesVerifyOnly`。该模式不保存或修改资产，只调用无世界上下文的数据工厂产生临时数据，并在 `Saved/VehicleTemplateImports` 写入验证报告。

脚本在创建前预检三个原图和所有目标资产；只新建不存在的资产。生成的资产带有 `SilverChoir.VehicleTemplates.Owner` 标签，纹理额外记录源文件 SHA256。重复运行会验证并复用本次资产，不自动重导入或覆盖；发现未标记的同名资产、变化后的原图、表数据或采样设置时会停止。用户后续编辑模板后不应将此一次性建表脚本当作同步工具。

验证内容包括：行结构、准确三行、2/4/6 座位、三张图片引用及导入设置、工厂创建三个有效且唯一的 GUID、来源模板行、满耐久/燃料、指定 `VehicleTemplate_Verification` 瓦片位置。此临时位置与 ID 不会写回模板表或玩家存档。

成功标记：`VEHICLE_TEMPLATES_OK rows=3 textures=3 factory=3`。原图真实尺寸、SHA256、表导出内容、导入设置与工厂验证结果记录在 `Saved/VehicleTemplateImports/<时间戳>.json`。

2026-09-28 验证完成：UE 5.8 已创建并保存三张纹理和数据表，在新进程以 `-VehicleTemplatesVerifyOnly` 重新加载成功，两个阶段均输出 `VEHICLE_TEMPLATES_OK rows=3 textures=3 factory=3`。工厂返回的座位数、图片引用、满耐久/燃料、指定瓦片及三个唯一 ID 均通过校验。

- 导入日志：`Saved/Logs/VehicleTemplatesImport.log`
- 新进程复验日志：`Saved/Logs/VehicleTemplatesVerify.log`
- 复验报告：`Saved/VehicleTemplateImports/20260927T180339127191Z.json`（文件名使用 UTC）

两个命令行进程均因已有的 EasyHouseBuilderEditor `SetWallOpenings is not AICallable` 启动日志返回退出码 1；车辆 Python 脚本均明确执行成功，复验中没有 Python 错误。此次只新增资源和导入脚本，无需编译 C++。
