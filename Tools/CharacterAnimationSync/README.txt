角色动画同步

制作源：R:/UE_WorkSpace/GameAnimationSample
游戏目标：S:/UE_WorkSpace/SilverChoir
共享入口：/Game/System/Object/Unit/Character/Animation
主动画蓝图：Female/ABP_Famale
Female 保存女性角色的选择表及动画配置；Shared 与 Support 保存依赖配置。
动画片段、网格体、材质仍使用原始包路径。BP_UnitPawn、测试单位、控制器、地图不属于同步入口。

后续工作：在制作项目中编辑共享动画蓝图和它引用的动画资源。保存并关闭两个编辑器后，运行 Sync-CharacterAnimations.ps1。
在 SilverChoir 根目录的 PowerShell 中执行：
  .\Tools\CharacterAnimationSync\Sync-CharacterAnimations.ps1 -Preview
  .\Tools\CharacterAnimationSync\Sync-CharacterAnimations.ps1
Preview 仅列出需要复制的数量和冲突，第二条命令实际同步。
脚本先用制作项目的 UE 资产注册表导出完整依赖清单，再检查 SHA256 并同步变化文件。
同步不会删除文件，不会复制项目源码、测试地图或测试角色蓝图。
首次导入可以覆盖现有依赖并备份；日常同步若发现游戏项目中的同名资源有本地修改，会停止并报告冲突。
备份及报告：游戏项目 Saved/CharacterAnimationSync。
目录移动由 UE 资产接口修复引用；残留重定向器用于兼容旧引用。不要在文件管理器中直接移动 uasset。
动画状态与音效标签从源项目的配置中筛选，写入目标 Config/Tags/CharacterAnimationSync.ini；其他游戏标签不参与同步。
两个项目目前均为 UE 5.8，并使用兼容的 HybridMotionSystem / HMS_Mover。
此脚本只同步资源。如果以后修改动画插件的 C++ 接口，需要另行合并插件代码并编译，再同步依赖新接口的动画资源。

点击移动：BP_GameMainMapPlayerController 的 BattleMoveOrders 图中，IA_BattleMove 的 Started 调用“通知选中单位移动到鼠标位置”。
默认右键移动、左键选择；按键可在 /Game/System/Input/Battle/IMC_Battle 修改。
返回值是接受命令的单位数量，OutError 可用于蓝图提示。没有完整导航路径的单位不会执行部分路径。
每个单位使用 UnitAIController + HMS_NavMover，动画状态经 HMSAnimationData 传给 ABP_Famale。
战斗子地图必须放置 NavMeshBoundsVolume，当前 RecastNavMesh 使用 Dynamic 以支持流送地图偏移。
L_BattleTemplate 和 T5/L_T5 已配置覆盖现有地形的 BattleNavigationBounds。
单位可通过“设置单位移动步态”切换 Walk / Run / Sprint，速度可在单位类默认值中调整。
初版为单机逻辑，暂不接入智能对象交互和网络命令 RPC。
