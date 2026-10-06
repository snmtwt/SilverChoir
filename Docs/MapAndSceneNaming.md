# Map 与 Scene 的职责

2026-09-23 起，原来的地图层统一使用 Map。Scene 专指地图内的功能场景，不对应单独的 UWorld。

| 原名称 | 当前名称 |
| --- | --- |
| Source/SilverChoir/Public/Scene、Private/Scene | Public/Map、Private/Map |
| /Game/System/Scene | /Game/System/Map |
| GameMainScene / BaseScene / BattleScene | GameMainMap / BaseMap / BattleMap |
| GameMainSceneGameMode 等地图类 | GameMainMapGameMode 等 |
| SceneWidgetBase | MapWidgetBase |
| ActivateLoadedScene / CurrentSceneType | ActivateLoadedMap / CurrentMapType |
| MapTransitionSystem 的 SubScene 接口 | SubMap 接口 |
| SceneID / SceneMap（地图配置） | MapID / MapAsset |

引擎自己的 USceneComponent、Scene Rendering 等概念不改名。DefaultEngine.ini 中保留旧类型、函数、属性、参数和资产包路径的精确 Core Redirects，用于兼容旧序列化引用。旧内容资产通过 UE AssetTools 迁移，并重新编译保存；不通过文件系统直接改名 uasset。

2026-10-02 已移除旧 SceneConfig/MapConfig 配置及旧战斗切换 API，相关重定向同时清理。地图加载/卸载改由 MapTransitionSystem 管理，ActivateLoadedMap 必须明确传入地图类型。

原有流程仍为：主菜单 → GameMainMap → 加载基地/战斗子地图 → ActivateLoadedMap 更新 GameState 并通过 PlayerController 切换对应地图 UI。

新插件 SceneManagementSystem 在当前 World 内管理功能场景，例如基地总览、建造、编队。SceneTag 在场景蓝图默认值配置，SceneClasses 在 `/Game/System/SubSystem/SceneSystem/BP_SceneManager` 配置。地图层不依赖这些具体功能，场景蓝图可调用 MapTransitionSystem 或区域块等业务接口。

区域块的 ReceiveRegionClicked（“区域被点击”）支持蓝图重写，可以在此调用“按标记切换场景”。原有 OnRegionClicked 分发器继续可用。

迁移前的 Source、Config、Content、Scripts、Docs、MapTransitionSystem 已备份到 Saved/MapRenameBackups/20260923-014339。新插件用法见 Plugins/SceneManagementSystem/README.md。
