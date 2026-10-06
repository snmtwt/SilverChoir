# 沙盘模型优化源文件

本目录保存 FBX、可编辑 Blender 工程及质量报告，不是 UE 资产目录。三个 LOD 已通过 UE 导入验证和画面对比，并已写回原资产 `/Game/Meshs/Map/SM_SandboxMap`。应用记录位于 `Saved/SandboxModelOptimization/promotion_result.json`。

原 UE 资产：`/Game/Meshs/Map/SM_SandboxMap`，原始面数为 **3,066,634**。

原始 FBX（只读使用，未修改）：
`S:/UE_WorkSpace/SC_Scene/Exports/SandboxNaturalV5_Combined/FBX/SandboxNaturalV5_TerrainAndWater_Import.fbx`

| 源文件 | Blender 三角数 | UE 构建后三角数 | LOD Screen Size |
|---|---:|---:|---:|
| [lod0.fbx](lod0.fbx) | 500,000 | 499,990 | 1.00 |
| [lod1.fbx](lod1.fbx) | 150,000 | 149,999 | 0.45 |
| [lod2.fbx](lod2.fbx) | 54,172 | 54,172 | 0.18 |

- 保留原点、轴向和厘米导入尺寸，UE 构建后 bounds 的最大差值为 0.125 cm（模型原尺寸为 24 km × 14 km）；保留地形/水面两个材质槽及 UE 材质引用。
- UV0 恢复原模型的线性 XY 映射；保留自定义法线，并导出 FACE 平滑组。全部顶点坐标及 UV 经 FBX 回读，最大误差为 0；最大法线方向误差低于 0.077°。
- 保留各 LOD 的原开口边界，LOD0 另保留海岸交叉顶点。远景 LOD1/2 允许岸线近似；水面动态 WPO 的插值变化不属于静态高度误差指标。

[sandbox_lods.blend](sandbox_lods.blend) 包含原模型参考及三个独立 LOD，默认仅显示 LOD0。详细指标见 [quality_report.json](quality_report.json)；局部反向三角的安全修复记录见 [local_repair_report.json](local_repair_report.json)。

原 UE 文件备份：`Saved/SandboxModelOptimization/Backup/SM_SandboxMap.uasset`（相对项目根目录）。恢复时应先关闭占用该资产的编辑器。

## 复现入口

- [blender_sandbox_lods.py](../../../Scripts/Editor/blender_sandbox_lods.py)：后台导入原 FBX，分材质减面、保护边界并生成 LOD；工作输出位于 `Saved/SandboxModelOptimization/Blender/`。
- [refine_blender_sandbox_lods.py](../../../Scripts/Editor/refine_blender_sandbox_lods.py)：恢复原 UV 映射，修复局部折叠并重新导出、回读和量化验证。
- [import_sandbox_lods.py](../../../Scripts/Editor/import_sandbox_lods.py)：`prepare_candidate()` 从本目录导入独立 UE 候选，恢复原设置并验证；通过审查后再单独执行原资产应用步骤。

Blender 脚本使用本机工程和原始地形数据的绝对路径；换机器复现前需调整脚本路径配置。

## 保存后验证（2026-10-01）

- 独立进程重新加载原资产，确认三个 LOD、材质引用及 LOD0 重导入路径均正确。资产大小由 65,004,500 字节降至 27,338,435 字节。
- `SilverChoir.MenuTravel.SandboxScene` 从主菜单进入基地的运行回归通过：1 成功、0 警告、0 失败；此次运行未复现沙盘的 CreateExport 外部容器加载错误。
- D3D12 实测此模型三个 LOD 的光追几何对象大小分别为 14.394、4.478、1.628 MB，总计 20.500 MB；原单档模型为 66.044 MB。这不是整个进程的显存用量，也未据此推断帧率收益。
- 截图、运行报告和几何内存 CSV 在 `Saved/SandboxModelOptimization/`。测试启动日志另有引擎 ToolsetRegistry 的 PythonTestRunner 错误；独立检查命令也有 EasyHouseBuilderEditor 工具注册错误导致进程退出码 1，但模型校验脚本本身成功。上述插件错误未在此次模型优化中修改。
