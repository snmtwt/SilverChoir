# SIS 物品图标捕获修复说明

日期：2026-09-26；引擎：UE 5.8。

## 修改位置

实际安装目录：`R:\EpicGame\UE_5.8\Engine\Plugins\Marketplace\Strategy0d4eb608a03cV2`。修改此引擎插件会影响使用该安装版本的其他工程。

## 已确认的问题

1. 原就绪检查只有注册和渲染状态，没有检查材质 shader map、网格编译或纹理流送。
2. 原静态捕获等待 2 秒就强制拍摄并缓存，资源未就绪的图像可能长期保留。
3. 创建预览后允许同帧立即拍摄，没有给渲染状态更新留下帧间间隔。
4. 同一静态捕获器没有保护待完成请求，多张图标并发请求会覆盖预览对象和待缓存状态。

## 新行为

- 等待实际参与 ShowOnly 的网格和材质资源；编辑器下检查网格、纹理编译状态。
- 使用当前场景 ShaderPlatform，检查 IsCompilationFinished 与 ShaderMap::IsValidForRendering。不能用 IsGameThreadShaderMapComplete 作为唯一条件：实际衬衣材质存在编译结束、可用 shader map 已创建但可选变体不齐全的情况。
- 请求材质纹理 mip 驻留，检查纹理资源、初始化/流送和完整驻留。
- 材质就绪后重建渲染状态，跨帧等待并使用渲染命令栅栏；捕获提交后再等待栅栏才缓存和释放。
- 超过 2 秒只输出一次等待原因，继续等待，不再把超时视为成功。
- 忙碌时借用现有捕获器池，隔离各请求的模型、RenderTarget 和 MID，完成后归还。
- Pending MID 改为反射强引用，避免异步等待期间被 GC 回收。

## 精确修改行

行号基于本次修复前备份与修复后文件；后续编辑可能移动行号。下方区间为差异块，完整代码见同目录 SIS_ItemCaptureFix.patch。

| 文件 | 修改前行 | 修改后行 |
|---|---:|---:|
| `Source/StrategyInventorySystem/Private/Tools/SIS_ItemModelCapturer.cpp` | 第 9 行后插入 | 10–16 |
| `Source/StrategyInventorySystem/Private/Tools/SIS_ItemModelCapturer.cpp` | 137–159 | 144–177 |
| `Source/StrategyInventorySystem/Private/Tools/SIS_ItemModelCapturer.cpp` | 第 349 行后插入 | 368–380 |
| `Source/StrategyInventorySystem/Private/Tools/SIS_ItemModelCapturer.cpp` | 372–393 | 403–405 |
| `Source/StrategyInventorySystem/Private/Tools/SIS_ItemModelCapturer.cpp` | 第 589 行后插入 | 602–605 |
| `Source/StrategyInventorySystem/Private/Tools/SIS_ItemModelCapturer.cpp` | 844–884 | 860–861 |
| `Source/StrategyInventorySystem/Private/Tools/SIS_ItemModelCapturer.cpp` | 889–954 | 866–909 |
| `Source/StrategyInventorySystem/Private/Tools/SIS_ItemModelCapturer.cpp` | 第 961 行后插入 | 917–919 |
| `Source/StrategyInventorySystem/Public/Tools/SIS_ItemModelCapturer.h` | 第 16 行后插入 | 17–17 |
| `Source/StrategyInventorySystem/Public/Tools/SIS_ItemModelCapturer.h` | 491–491 | 492–492 |
| `Source/StrategyInventorySystem/Public/Tools/SIS_ItemModelCapturer.h` | 501–501 | 502–510 |
| `Source/StrategyInventorySystem/StrategyInventorySystem.Build.cs` | 第 17 行后插入 | 18–18 |
| `Source/StrategyInventorySystem/StrategyInventorySystem.Build.cs` | 第 29 行后插入 | 31–31 |

## 验证

- 插件 Win64 Editor 独立重建通过，已将更新后的二进制和生成文件安装回原引擎插件目录。
- SilverChoirEditor 编译通过。
- 实际 RHI 渲染测试 `SilverChoir.Inventory.CaptureReadiness` 通过，使用 `/Game/System/Object/Items/BP_衬衣`。
- 验证了创建帧不立即完成捕获、两个同帧请求使用独立 MID/捕获器、等待完成后输出可见图像。实际生成的衬衣呈白色并具有模型明暗细节。
- 测试日志：`Saved/Logs/ItemCaptureVerified.log`；结果 `Test Completed. Result={Success}`。
- 图像：[ShirtCapture.png](../../Saved/Screenshots/ItemCapture/ShirtCapture.png)。该回归测试使用默认取景角度，并非修改游戏中的物品取景配置。
- 未进行 Development/Shipping 打包验证，也未人为模拟所有资源损坏或蓝图延迟赋材质情形。

## 使用与限制

- 重启编辑器/游戏以清除会话中的旧图标缓存；已有离线导出图片需要重新生成。
- 若材质本身编译失败或纹理始终不能驻留，会保持等待并输出 SIS capture waiting for resources，不会缓存未就绪的结果。
- 本次检查引擎资源就绪状态，不能推断物品蓝图在将来某个异步回调中是否还会替换材质，也不会自动补齐本来就未配置的材质槽。
- 当前安装验证为 Win64 Editor。打包游戏前，应对修改后的源码重新构建对应 UnrealGame Development/Shipping 插件，不能沿用旧的预编译游戏模块。

## 备份与移植

修改前源码、编辑器二进制和中间产物备份：`S:\UE_WorkSpace\SilverChoir\Saved\PluginFixBackups\SISCapture-20260926`。

移植时按补丁修改三个文件，使用 BuildPlugin 重建插件，再替换匹配引擎版本的二进制及生成文件。仅修改引擎目录中的源代码，项目编译通常仍会使用原来的预编译模块。

项目中新增回归测试：`Source/SilverChoir/Private/Tests/ItemCaptureReadinessTests.cpp`；此文件不属于插件必要补丁。
