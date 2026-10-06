# 单位 Pawn 基类

`Public/Object/Unit/UnitPawnBase.h`，对应实现位于 Private。蓝图父类：UnitPawnBase。

参考 `R:/UE_WorkSpace/GameAnimationSample/Source/GameAnimationSample/Private/Test/MWTestPawnBase.cpp` 的胶囊、网格和 HMS Mover 输入接入方式。组件为 Collision、Mesh、UHMS_CharacterMoverComponent 和 USIS_UnitInventoryComponent。

在子蓝图中配置 Mesh、动画蓝图和库存设置。基类不引用参考工程中的模型、蒙太奇或动画资产。HMS 完整动画、导航、智能物体和布娃娃流程仍需按玩法扩展。

“设置单位移动速度”接受世界空间水平速度(cm/s)，持续生效，传零停止；通过 IMoverInputProducerInterface 同时提供引擎与 HMS 输入。没有自动占用玩家控制器或绑定键盘。Mover 管理移动同步；单位业务数据没有因此自动网络同步。

“绑定单位数据”通过玩家单位子系统查询共享实例。C++ 使用 GetUnitDataShared()，经 Modify 修改后收到事件，蓝图收到“单位数据已更新”。换绑、EndPlay、BeginDestroy 均解绑并释放引用。库存组件与 FUnitData.InventoryItems 的导入导出尚未自动连接。

验证：SilverChoir.Unit.PawnBase，覆盖生成、组件注册、HMS 输入、共享引用与销毁释放。

穿戴组件采用 LeaderPose 外观配置：在网格赋值和注册之前禁用独立动画、模型自带的后处理动画蓝图、刚体模拟/刚体重力/碰撞通知、动画完成时的重叠更新及独立组件复制。不会分配衣服自己的刚体物理状态。保留网格自带的布料模拟，以及用于布料、LOD、Morph 和材质曲线同步的组件 Tick；不再独立计算骨骼姿势。保留正常阴影、渲染和衣服自身包围盒，避免宽裙/外套超出身体范围时被提前裁剪。衣服需在资源中配置布料数据才会出现布料效果，节点不会自动生成布料配置。

穿戴外观直接由 UnitPawnBase 提供，无需额外子类。蓝图节点“添加穿戴骨骼网格体”接受 SkeletalMesh 与 Guid Key，返回新创建的 SkeletalMeshComponent。组件归单位所有，附着到 MeshComponent，局部变换为单位变换，并以 MeshComponent 为 LeaderPose；关闭碰撞、重叠和导航影响。服装骨骼需要与身体的骨骼名称和层级兼容，无须另配动画蓝图。

相同 Key 再次添加会销毁旧组件并替换；空模型或无效 Guid 返回空且保留旧组件。“删除穿戴骨骼网格体”按 Key 销毁组件，成功返回 true，不存在返回 false。建议使用装备物品的 ItemId 作为 Key，在蓝图装备/卸装业务事件中调用。两个节点只管理本地运行时外观，不修改库存或 UnitData，也不自动网络同步；简单 NPC 不调用即可。

`AWearableItem`（穿戴物品）继承 `AStaticMeshItem`，已在 C++ 实现这两个装备事件。继承的 MeshComponent 用于配置物品在地面/展示中的静态网格和材质；WearMesh 单独配置穿到角色身上的骨骼网格，使用该骨骼网格自己的材质。将物品模板的 ItemActorClass 指向该蓝图即可。OnAddToEquipmentSlot 根据事件的目标单位与 ItemId 添加穿戴组件，不复制静态展示网格的材质；OnRemoveFromEquipmentSlot 根据同一目标和 ItemId 删除组件。目标角色未提供时从 TargetInventory 的 Owner 解析单位。穿戴不依赖是否配置了静态展示模型。

该类不缓存穿戴者、不移动物品对象池中的 Actor，因此可供多个角色穿同款服装，也支持展示单位和战斗单位同时使用同一单位物品数据。卸装事件的 bHandledItemActorEvent 保持 false，已有物品实体的销毁仍交给 SIS。蓝图重载装备/卸装事件时，请调用父实现保留自动穿脱逻辑。验证：SilverChoir.Inventory.WearableItem。
