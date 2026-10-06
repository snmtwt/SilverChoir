# 人员整备室：库存分页与程序选中

## 调用入口
- `SelectInventoryPage` / 切换库存页：0—4 对应 01—05。先记录 CurrentInventoryPage，再调用蓝图事件；重复页码不触发。
- `OnInventoryPageChanged` / 库存页已切换：PageIndex 为新页，PreviousPageIndex 为旧页，InventoryContainer 为复用容器。
- `SelectUnitById` / 根据单位ID选中人员：传 FGuid。需先加载人员列表；执行当前瓦片、筛选和关闭状态校验，然后更新选中项并触发 OnPersonnelClicked，与列表点击一致。
- `RestoreInventoryPageSelection` / 恢复库存页选择：只恢复高亮和页码，不发出切换事件。

## 数据存储
玩家处理类 UPlayerManagerBase 的 InventoryPages 默认含五个 FPlayerInventoryPage，每页保存一个 FSIS_ItemData 数组。C++ 节点只读写数据，不调用库存插件：
- 保存库存页物品 / WriteInventoryPage
- 读取库存页物品 / ReadInventoryPage
- 记录当前装载库存页 / SetActiveInventoryPage

通过玩家函数库“获取玩家处理类”调用以上节点。无效索引返回 false；读取失败输出空数组，写入失败不改变其他页。数据保存在 GameInstance 所属管理对象中，不等同于磁盘存档；SaveGame 标记不会自动写盘，插件嵌套物品字段需由实际存档流程处理。

## C++ 仓库会话（2026-09-26）
人员整备室使用 UWarehousePageSession 统一处理首次加载、切页和跨页拖拽。插件源码没有修改。
- InitializeWarehouseSession / 初始化仓库会话：在加载场景 UI 时调用，蓝图 OnSceneUIOpened 也可调用；已有会话不会重新覆盖库存。
- SelectInventoryPage：先保存旧页、读取新页、注册同一个容器，再更新选择并发出 OnInventoryPageChanged。
- OnInventoryPageChanged 现在是完成后的表现通知，保留当前和上一页参数，不应再执行保存/加载业务。现有蓝图旧读写链已断开，原有动画继续使用。
- 首次进入读取 ActiveInventoryPage（默认 0）；重新打开保留实时库存。若在同一组件上开始新游戏/读档，应先关闭仓库，再写入页数据并调用 ResetInventoryPageInitialization。

## 拖拽悬停切页
界面 C++ Tick 检测 SIS 当前拖拽对象和五个按钮。默认停留 0.5 秒、允许 4 屏幕像素抖动；蓝图默认值可修改 DragPageHoverSeconds 和 DragPageCursorTolerance。离开按钮、累计移动超出容差、换目标或结束拖拽都会重置计时。当前页不重复触发。

拖拽开始记录来源页。跨页时保留玩家原库存组件作为来源，额外创建一个无界面的临时 SIS 单位库存组件作为目标；切换更多目标页时复用该组件，始终只有一个可见库存容器。返回来源页时重新显示原组件。

SIS 的 OnDropDragItem 发生在实际移动之前，不能据此删除来源页。会话在插件清除当前拖拽对象之后保存双方真实数据：成功移动后来源已由插件更新，取消/拒绝则来源仍保留。最后将当前页恢复到玩家原库存组件并释放临时组件。保存完整 SIS 序列化结果，不手工按 ID 删除父物品而遗漏附件或内部物品。

当前跨页拖拽支持 Standalone 单机运行。联网拖拽期间拒绝切页，避免客户端临时组件参与服务器移动；联网扩展需要服务器持有的仓库组件和确认流程。

自动测试：SingleWarehouse（首次加载、切页、重新打开）；WarehouseCrossPageDrag（悬停计时、跨多页/返回、取消、通过 SIS 实际移动及双页数据保存）。
当前页的实时修改仍在玩家库存组件中；切换离开该页时才更新管理类快照。若需要退出游戏存档，请先导出当前页再保存全部页数据。

业务顺序集中在项目 C++ 会话类中；蓝图负责界面布局、动画和切页后的表现。
