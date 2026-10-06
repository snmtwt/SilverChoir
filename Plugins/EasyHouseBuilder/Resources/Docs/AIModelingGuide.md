# Parametric Building Toolset AI 建模指南

AI 代理在通过 MCP 编辑 EHB 建筑之前，必须先调用 `GetToolsetCapabilities` 和 `GetModelingGuide`，并严格遵守前置检查流程。

## 前置检查

在创建任何建筑元素之前，先调用 `GetToolsetCapabilities`、`GetModelingGuide`、`GetStatus`。

必须同时满足：

1. `bHasEditorWorld=true`
2. Current-building rule: reuse the existing `EHB_Building` returned by `GetActiveBuilding()` after `GetStatus()`. Do not call `CreateBuilding` as a fallback, and do not create a new root building object for ordinary AI generation or modification.
3. `bIsBuildingModeActive=true`
4. `bHasActiveBuilding=true`
4. `ActiveBuilding` 指向一个有效的 `EHB_Building`

注意：`SelectedBuilding` / `bHasSelectedBuilding` 只是兼容字段。这里的“选中建筑对象”不是 UE 编辑器选择集里的 Actor，而是建筑模式内部的当前 `ActiveBuilding`。只有在建筑模式下选择其他 `EHB_Building` 时，建筑模式才会切换当前建筑对象。

如果 `bIsBuildingModeActive=false`：

- 停止使用 EHB Toolset 创建建筑元素。
- 提醒用户：当前工程中已经引入 EHB 插件，但编辑器尚未进入建筑模式。
- 询问用户是否改用默认 Actor 方式建造房屋。
- 不要自动切换到默认 Actor 方案，必须等待用户确认。

如果 `bHasActiveBuilding=false` 或 `bHasSelectedBuilding=false`：

- 停止创建。
- 提示用户在建筑模式下选择一个 `EHB_Building` 对象。
- 用户选中后，再重新调用 `GetStatus`。

## 插件边界

EHB 插件仍在开发中，AI 只能创建当前 MCP Toolset 明确提供的元素和流程。

- 可以使用：`GetToolsetCapabilities`、`GetModelingGuide`、`GetStatus`、`GetActiveBuilding`、`CreatePillar`、`ConnectPillars`、`CreateFloorSlab`、`SetFloorSlabTopPolygon`、`InsertFloorSlabCorner`、`MoveFloorSlabCorner`、`RemoveFloorSlabCorner`、`CreateRoomFilledFloorSlabAtPoint`、`CreateRoomFilledFloorSlab`、`AddFloorSlabRectangularHole`、`AddFloorSlabCircularHole`、`GetStairFoundationPlan`、`CreateStairFromBottom`、`CreateStairBetweenLandings`、`CreateStair`、`SetElementFloorAssignment`、`GetFloorSummary`、`AddDoorWindow`、`SetWallCurve`、`CreateGableRoof`、`CreateHipRoof`、`CreateRectangularRoom`、`CreateRectangularFloorPlan`、`GetBuildingSnapshot`。
- 不要创建插件未提供的元素类型。
- 不要为了满足用户描述而自行发明新的 EHB 元素。
- 如果用户要求创建当前 Toolset 未提供的元素，应说明该元素暂未由插件提供，并提示用户自行创建或等待插件后续支持。不要自行创建插件没有提供的建筑元素。

## 核心模型

Parametric Building Toolset 是关系驱动的建筑系统，不是普通 Actor 生成系统。

- `Building` 是所有建筑元素的根对象和拥有者。
- `Pillar` Actor 是拓扑节点。
- `Wall` Actor 是通过连接两根柱子创建出来的拓扑边。
- `DoorWindow` Actor 是依附在已有墙体上的门窗洞口。
- `FloorSlab` Actor 可用于地基、楼板或顶板，但普通层板轮廓必须通过“填充房间”逻辑生成。
- `Stair` Actor 是插件提供的程序化楼梯元素，必须通过 `CreateStairFromBottom`、`CreateStairBetweenLandings` 或 `CreateStair` 创建。
- `Roof` Actor 是覆盖闭合建筑体量的屋顶元素。山形屋顶使用 `CreateGableRoof`，四坡/四边/hip 屋顶使用 `CreateHipRoof`。
- 楼层关系由每个元素的 `FloorIndex` 和 `FloorRole` 维护，Building 会据此重建楼层索引和闭合房间缓存。
- 建筑局部坐标使用厘米作为单位。
- 建筑房屋时，以 `Building` Actor 的位置作为中心点进行创建。默认房屋中心应位于 Building 局部坐标 `(0,0,0)`。

## 住宅尺度约束

AI 生成房屋时必须优先保证真实可用尺度，而不是只满足拓扑闭合。

- 默认按 Unreal / EHB 厘米单位建模。
- 单层住宅外轮廓建议不小于 `900cm x 700cm`；多房间住宅建议不小于 `1200cm x 900cm`。
- 普通房间短边不应小于 `300cm`；卧室建议不小于 `330cm x 360cm`。
- 客厅建议不小于 `420cm x 500cm`。
- 厨房、卫生间短边不应小于 `180cm`；卫生间建议至少 `200cm x 240cm`。
- 走廊净宽不应小于 `100cm`，推荐 `120cm`。
- 门宽建议 `80-100cm`，门高约 `210cm`。
- 默认墙高使用 `300cm`；住宅墙高/层高建议 `300-330cm`。多层建筑中 `FloorHeight` 通常应等于 `WallHeight`；如果选择 `320cm` 层高，应同时把 `WallHeight` 和 `FloorHeight` 设为 `320cm`，不要额外叠加楼板厚度。
- 放置楼梯前必须先预留楼梯间、洞口和上下落脚平台，不要在房间切完后硬塞楼梯。
- 直跑或折返楼梯的占地至少预留 `280cm x 420cm`，舒适尺寸建议 `320cm x 480cm`。
- 楼梯宽度不应小于 `90cm`，推荐 `100-120cm`。
- 如果建筑尺寸不足以容纳楼梯，应自动扩大建筑外轮廓、减少房间数量或改为更简单的交通组织。

## 黄金规则

AI house consistency rule: structural pillar Width and Depth must equal the connected WallThickness. In high-level recipes, set PillarSize=WallThickness.

1. 不要使用通用编辑器 Actor 工具直接创建 `EHB_Wall`。
2. 创建墙体时，必须先为墙体两端调用 `CreatePillar`，再调用 `ConnectPillars`。
3. 不要直接创建 `EHB_DoorWindow`。必须在已有墙体上调用 `AddDoorWindow`。
4. 不要在建筑对象之外创建游离的 EHB 元素。元素必须归属于建筑模式下当前的 `ActiveBuilding`。
5. 默认情况下，先创建地基，再创建柱、墙、门窗、层板等上层建筑元素。
6. 地基可以使用 `CreateFloorSlab` 创建。
7. 普通层板、顶板、房间楼板必须优先使用 `CreateRoomFilledFloorSlabAtPoint`，也就是插件的“填充房间”功能创建，不要由 AI 自行指定层板多边形。若当前房间计划使用曲墙，必须先调用 `SetWallCurve` 完成墙体弧度，再填充房间层板/顶板。
8. 异形地基或异形层板使用 `CreateFloorSlab` 创建，或用 `SetFloorSlabTopPolygon` / `InsertFloorSlabCorner` / `MoveFloorSlabCorner` / `RemoveFloorSlabCorner` 编辑已有层板。
9. 层板挖洞必须使用 `AddFloorSlabRectangularHole` 或 `AddFloorSlabCircularHole`，不要创建独立网格假装洞口。
10. 创建楼梯必须使用 `CreateStairFromBottom`、`CreateStairBetweenLandings` 或 `CreateStair`。AI 默认优先使用 `CreateStairFromBottom`；转角或弯曲楼梯使用 `CreateStairBetweenLandings`。不要调整楼梯中间控制器。
11. 创建多层建筑时，必须让每个元素获得正确的 `FloorIndex` 和 `FloorRole`；必要时调用 `SetElementFloorAssignment` 校正。
12. 只有当用户明确要求清空或重新生成建筑时，才调用 `ClearBuilding`，并传入 `bConfirm=true`。
13. 大型房屋或多房间平面优先使用 `CreateRectangularFloorPlan`，不要反复调用 `CreateRectangularRoom` 造成重复柱墙。
14. 单个矩形房间才使用 `CreateRectangularRoom`，并默认传入 `LocalCenter=(0,0,0)`，让房屋围绕 Building Actor 位置居中。
15. 修改完成后，调用 `GetBuildingSnapshot` 和 `GetFloorSummary` 验证关系、楼层、闭合房间环和已创建元素。
16. 屋顶应在顶层墙体和主要闭合体量稳定后创建。山形屋顶使用 `CreateGableRoof`；用户要求“四坡屋顶”“四边屋顶”“hip roof”“four-slope roof”“four-sided roof”时，使用 `CreateHipRoof`。

## 推荐 MCP 调用顺序

1. `GetToolsetCapabilities`
2. `GetModelingGuide`
3. `GetStatus`
4. 如果未进入建筑模式，提示用户当前工程已引入 EHB 插件但未进入建筑模式，并询问是否改用默认 Actor 建造房屋。
5. 如果建筑模式下没有当前 `ActiveBuilding`，提示用户在建筑模式下选择 `EHB_Building` 后再继续并停止。
6. `GetActiveBuilding`
7. 以 Building Actor 位置作为房屋中心点规划建筑，默认中心为局部坐标 `(0,0,0)`。
8. 创建地基：通常调用 `CreateFloorSlab(..., bFoundation=true, bKeepFoundationBottomOnGround=true)`。
9. 创建上层结构：
   - 对每个墙体端点调用 `CreatePillar`
   - 对每段墙调用 `ConnectPillars`
   - 对门窗洞口调用 `AddDoorWindow`
10. 大型多房间建筑改用 `CreateRectangularFloorPlan`，让工具复用共享柱墙。
11. 创建普通层板或顶板：调用 `CreateRoomFilledFloorSlabAtPoint`，不要手动指定层板轮廓。
12. 填充层板时，`RoomInteriorPoint` 必须在目标房间内部，通常使用 `GetBuildingSnapshot.closedLoops[].interiorPointHint` 或房间中心；不要把层板原点放在共享墙上。
13. 如需异形地基或异形层板，调用 `SetFloorSlabTopPolygon` 或控制点编辑函数修改轮廓。
14. 规划室内门和动线：每个主要房间必须通过门连到走廊、门厅、客厅或楼梯间；不要留下无门封闭房间。
15. 如需楼梯，先确定楼梯间和上下落脚平台，再对目标层板调用 `AddFloorSlabRectangularHole` 或 `AddFloorSlabCircularHole`。
16. 调用 `CreateStairFromBottom`、`CreateStairBetweenLandings` 或 `CreateStair` 创建插件楼梯元素；如果楼梯与地基或室外台阶衔接，先调用 `GetStairFoundationPlan`。
17. 对多层建筑，检查或校正元素 `FloorIndex` / `FloorRole`。
18. 创建屋顶：对山形屋顶调用 `CreateGableRoof`；对四坡/四边/hip 屋顶调用 `CreateHipRoof`。屋顶中心和尺寸应来自顶层闭合墙体体量，不要来自阳台、露台、台阶或地基外扩。
19. `GetBuildingSnapshot`
20. `GetFloorSummary`

## 推荐 AI 工作方式

AI 创建完整房屋时，不要一口气把所有元素随机铺开。应先建立建筑逻辑，再按楼层循环推进。

1. 设计房屋逻辑：先确定建筑用途、入口、交通动线、房间功能、房间相邻关系、楼梯位置大概区域、每层楼层高度和真实可用尺度。涉及楼梯时，先定楼梯间和交通空间，再切分房间。
2. 设计地基形状与高度：先创建或编辑地基。若存在室外台阶、入户台阶或楼梯衔接，先调用 `GetStairFoundationPlan`，让地基高度和台阶高度一致。
3. 按楼层设计柱子、墙壁和空间分隔：从 `FloorIndex=1` 开始逐层创建柱、墙，形成闭合房间。大型平面优先用 `CreateRectangularFloorPlan`，复杂平面先规划所有共享端点，再创建柱墙。
4. 为当前楼层房间添加层板：优先使用 `CreateRoomFilledFloorSlabAtPoint`，`RoomInteriorPoint` 必须在目标房间内部。若该楼层包含曲墙，先设置曲墙弧度，再填充层板。创建楼梯前，先规划楼梯间，并在目标层板中挖出足够人上楼的洞口。
5. 添加楼梯：楼梯必须位于房间、楼梯间、走廊或门厅的可通行区域内，上下端点要有落脚平台，不能贴墙、穿墙或朝墙结束。
6. 添加门窗：先保证每个主要房间有合理入口，再添加外窗。卧室、厨房、卫生间、楼梯间等主要空间必须通过门连接到走廊、门厅、客厅或相邻公共空间。门窗必须依附已有墙体，使用 `AddDoorWindow` 一次性传入宽、高和距地高度。
7. 检查当前楼层是否适合人使用：调用 `GetBuildingSnapshot` 和 `GetFloorSummary`，检查房间是否闭合、是否有入口、楼梯是否连通上下层、洞口是否覆盖楼梯、门窗是否在合理墙段、层板是否填充到正确房间、各元素 `FloorIndex/FloorRole` 是否正确。
8. 当前楼层通过检查后，再开始下一层设计。下一层墙底 Z 应等于上一层墙顶 Z，不要额外加楼板厚度。

楼层可用性检查重点：

- 每个主要房间应有门或与走廊/楼梯间连通，不应生成无法进入的封闭空间。
- 内部隔墙如果分隔出卧室、厨房、卫生间、储藏间或楼梯间，必须至少有一个门洞；不要只画墙不留门。
- 楼梯不应直接怼墙，楼梯底部和顶部应有至少一个可站立平台。
- 楼梯洞口应大于楼梯宽度和通行长度，并与楼梯实际位置对齐；楼梯上端必须落在上层楼板或平台高度，下端必须落在下层可通行地面。
- 普通层板应通过房间内部点填充到目标房间，不应填到相邻房间。
- 二层及以上不应与下一层之间留出楼板厚度导致的缝隙。
- 门窗应避开柱子、墙角和楼梯洞口；窗户应有合理距地高度。

## 正确的墙体创建流程

正确做法：

```text
Building = GetActiveBuilding()
P0 = CreatePillar(Building, "P0", (-200,0,0))
P1 = CreatePillar(Building, "P1", (200,0,0))
Wall = ConnectPillars(Building, P0, P1)
```

错误做法：

```text
SpawnActor(EHB_Wall)
Create generic wall mesh without pillars
Move wall endpoints without updating pillar topology
```

## 矩形房间 Recipe

大型多房间房屋优先使用：

```text
CreateRectangularFloorPlan(
    Building,
    "House",
    Rooms=[
        { Name="Living", LocalCenter=(0,0,0), Width=600, Depth=500, FloorIndex=1 },
        { Name="Kitchen", LocalCenter=(500,0,0), Width=400, Depth=500, FloorIndex=1 }
    ],
    WallHeight=300,
    WallThickness=20,
    PillarSize=20,
    FloorHeight=300,
    bCreateFoundation=true,
    bCreateCeilingSlabs=true,
    FoundationPadding=20,
    FoundationThickness=20)
```

该调用会复用相同坐标的柱子和共享墙，适合大型房屋、多个房间和多楼层草图。

多层建筑中，`FloorHeight` 应优先等于 `WallHeight`。不要设置成 `WallHeight + SlabThickness`；层板厚度会向下一层内部嵌入，不应在两层墙体之间额外留空间。

创建单个房间时，优先使用：

```text
Status = GetStatus()
Building = GetActiveBuilding()
CreateRectangularRoom(
    Building,
    "RoomA",
    LocalCenter=(0,0,0),
    Width=600,
    Depth=400,
    WallHeight=300,
    WallThickness=20,
    PillarSize=20,
    bCreateFoundation=true,
    bCreateCeilingSlab=true,
    FloorIndex=1,
    FoundationThickness=20)
GetBuildingSnapshot(Building)
```

该调用会以 Building Actor 的位置作为中心点，先创建地基，再创建四根柱子和四面具有拓扑关系的墙体；如果启用顶板，会通过“填充房间”逻辑创建顶板。

## 层板与填充房间

普通层板不要让 AI 自行构造多边形。

- 地基：使用 `CreateFloorSlab`。
- 房间层板 / 顶板：AI 优先使用 `CreateRoomFilledFloorSlabAtPoint`。
- `CreateRoomFilledFloorSlabAtPoint` 需要一个位于目标房间内部的 `RoomInteriorPoint`，并由插件的“填充房间”功能自动计算房间轮廓。
- `CreateRoomFilledFloorSlab` 是高级兼容接口，只有明确知道 `AnchorWall` 和 `AnchorSide` 时才使用。
- 不要把层板 Actor 原点放在共享墙、外墙边、柱子中心或房间外侧；这会让填充算法选到隔壁房间。
- 创建后调用 `GetBuildingSnapshot`，检查该层板的 `localLocation` 是否位于目标房间内部，`localTopPolygon` 是否覆盖正确房间。
- 如果房间闭合关系尚未形成，先完成柱墙拓扑，再创建层板。
- 如果房间包含曲墙或凸窗弧墙，先通过 `SetWallCurve` 完成弧度修改，再创建层板/顶板；使用 `CreateAIHouseFromRoomPlan` 生成这类房间时，应先传 `bCreateRoomSlabs=false`，弯墙完成后再按房间内部点填充层板。
- 异形地基或异形层板：使用 `CreateFloorSlab` 传入多边形，或对已有层板使用 `SetFloorSlabTopPolygon`。
- 控制点编辑：使用 `InsertFloorSlabCorner`、`MoveFloorSlabCorner`、`RemoveFloorSlabCorner`，不要直接改 Actor 网格。

## 层板挖洞

层板洞口必须由插件工具写入层板数据，不要使用普通 Actor、普通网格或临时几何体假装洞口。

- 矩形洞：使用 `AddFloorSlabRectangularHole`。
- 圆形洞：使用 `AddFloorSlabCircularHole`。
- 洞口坐标使用目标 `FloorSlab` 的 `LocalTopPolygon` 坐标空间，通常与 `Building` 局部 XY 布局一致。
- 洞口必须位于层板外轮廓内部；如果函数返回失败，停止并调用 `GetBuildingSnapshot` 检查目标层板。
- 楼梯洞优先使用矩形洞，洞口尺寸应略大于楼梯宽度和通行长度。
- 楼梯洞不是视觉孔洞，必须服务于真实通行：洞口中心线应与楼梯中心线对齐，洞口长度覆盖从下端到上端的通行投影并额外留 `30-60cm`，洞口宽度至少为 `StairWidth + 40cm`。

## 屋顶

屋顶是覆盖闭合建筑体量的建筑元素，不是每个房间、阳台、露台、楼梯平台或装饰凸起都单独生成一片屋顶。

- 山形屋顶：使用 `CreateGableRoof`。
- 四坡屋顶 / 四边屋顶 / hip roof / hipped roof / four-slope roof / four-sided roof：使用 `CreateHipRoof`，不要用山形屋顶硬凑。
- 屋顶应在顶层墙体、主要房间体量和楼层高度稳定后创建。通常先创建主体墙体和顶层闭合体量，再创建一个主屋顶。
- `LocalCenter` 是屋顶覆盖体量的中心，使用 Building 局部坐标。普通楼层屋顶的 Z 通常等于该楼层墙顶高度。
- `Length` 和 `Width` 是屋顶局部坐标下、檐口外扩前的覆盖尺寸，应来自曲墙和门窗都稳定后的封闭墙体外轮廓，不要把阳台、露台、入口台阶、低平台或地基外扩算进去。AI workflow 的 `roofs` 可设置 `autoFitToWallFootprint=true`，或省略 `localCenter`/`length`/`width` 让工具按当前墙体闭环自动取尺寸。
- `YawDegrees` 用来对齐屋顶矩形和建筑翼体方向。建筑翼体旋转时，不要把 `YawDegrees` 固定为 0。
- `AxisMode` 控制屋脊方向。普通矩形体量通常让屋脊沿较长方向。
- `PitchDegrees` 普通住宅建议约 `18-35` 度，除非用户明确要求陡坡或缓坡。
- `EaveOffset` 是檐口出挑，普通住宅建议约 `25-50cm`。不要同时使用过大的 `Length/Width` 和过大的 `EaveOffset`。
- 普通可见山形屋顶通常保持 `bGenerateRidge`、`bGenerateEaves`、`bGenerateGableRakes`、`bGenerateGableEndWalls` 为 true。
- 普通可见四坡屋顶通常保持 `bGenerateRidge`、`bGenerateEaves`、`bGenerateHipRidges` 为 true。四坡屋顶没有三角形山墙端墙，不要传入或发明山墙端墙参数。
- 多个屋顶只用于明确分离的体量，例如主翼、垂直副翼、车库、阁楼窗体量或低一层的独立侧翼。普通房屋优先使用少量干净的大屋顶。
- 屋顶交叉切割时，先创建主屋顶，再创建需要被裁回的次屋顶。只在需要自切割的屋顶上启用 `bCutCollidingElements`。
- 另一个屋顶不会因为当前屋顶启用 `bCutCollidingElements` 而自动被切割；对方也要启用该开关才会切割自身。
- 需要阁楼窗户或墙体脚印开洞时，保持 `bUseWallFootprintCutters=true`。普通墙体组合会转换为脚印切割体，再切屋顶。
- `WallFootprintPadding` 建议使用小负值如 `-2` 或 `-4`，让开洞略小一点，便于墙体盖住切边。正值会放大洞口，容易露出缝隙。
- `WallFootprintMaxDimension=0` 表示不限制墙体脚印洞口大小。只有需要拒绝异常大洞时才设置具体上限。
- 调试屋顶切割时可临时启用 `bShowCutDebugVisualization`，完成诊断后关闭。

## 楼梯

楼梯必须通过 `CreateStairFromBottom`、`CreateStairBetweenLandings` 或 `CreateStair` 创建，不要直接 `SpawnActor(EHB_Stair)`。

- AI 默认优先使用 `CreateStairFromBottom`。
- 需要转角、弯曲或同时指定上下端点方向时，使用 `CreateStairBetweenLandings`。
- 楼梯宽度不应小于 `90cm`，推荐 `100-120cm`；楼梯间平面至少预留 `280cm x 420cm`，舒适尺寸建议 `320cm x 480cm`。
- `CreateStairFromBottom.LocalBottomLocation` 是楼梯下端落地点，使用 `Building` 局部坐标，Z 通常等于下层地面高度。
- `CreateStairFromBottom.UpDirectionYawDegrees` 表示从楼梯下端看向上层平台的方向。这个方向符合人类描述里的“上楼方向”。
- `CreateStairBetweenLandings.LocalBottomLocation` 是下端落地点；`BottomUpDirectionYawDegrees` 表示从下端看向上层的方向；`LocalTopLocation` 是上层平台真实位置；`TopDownDirectionYawDegrees` 表示从上层平台看向下端的方向。
- `CreateStair.LocalTopLocation` 是楼梯上端平台的真实位置，Z 应等于上层平台高度；`CreateStair.YawDegrees` 表示从上端向下端延伸的方向。
- AI 不要调整 `Stair` 的中间控制器；楼梯弯曲只通过上端位置/方向和下端台阶位置/方向控制。
- 楼梯必须服务于交通动线，不要直接贴外墙或怼到墙体上。上下端点都应位于房间或楼梯间内部，并与墙体保持至少 `max(60cm, StairWidth/2)` 的净距。
- 创建楼梯前先规划楼梯间和洞口：洞口长度应覆盖 `stairLength` 并额外留 30-60cm，洞口宽度应大于 `StairWidth` 至少 40cm。
- 楼梯底部和顶部都要有落脚平台；不要把 `LocalBottomLocation` 或 `LocalTopLocation` 放在墙中心线、柱子中心、门窗洞口或房间外侧。
- 楼梯与层板必须连成可走的整体：下端 Z 对齐下层地面/层板顶面，上端 Z 对齐上层楼板或平台顶面；如果上层层板存在，必须先挖洞再建楼梯，不能让楼梯穿过实心层板或停在层板下方。
- 楼梯上端平台应与上层房间、走廊或门厅连通；下端平台应与下层入口、走廊或公共空间连通。不要把楼梯放进没有门的孤立房间。
- 优先把楼梯放在专门的楼梯间、走廊、门厅或房间角落内侧，并让上楼方向朝向可通行空间，而不是朝向墙体。
- 不要把 `CreateStair.LocalTopLocation` 传成下层地面点，否则楼梯会落到错误高度；不确定时改用 `CreateStairFromBottom`。
- 创建穿层楼梯时，先在上层层板挖洞，再调用楼梯创建函数。
- 创建后调用 `GetBuildingSnapshot`，检查快照中的 `generatedStepCount`、`generatedStepHeight` 和 `stairLength`。

## 地基与台阶高度

当地基、室外入口台阶或楼梯相互衔接时，AI 必须先调用 `GetStairFoundationPlan`，再创建地基和楼梯。

- 已知用户想要的台阶高度时，传入 `DesiredStepHeight`，并使用返回的 `recommendedFoundationThickness` 作为地基厚度。
- 已知地基高度时，传入 `FoundationThickness`，并使用返回的 `recommendedStairMinStepHeight` / `recommendedStairMaxStepHeight` 作为楼梯步高约束。
- `CreateRectangularRoom` 和 `CreateRectangularFloorPlan` 都可以传入 `FoundationThickness`；不要再假设地基永远是 20 cm。
- `GetStairFoundationPlan.stepHeight` 是最终建议的单级台阶高度；如果 `foundationHeightDelta` 明显不为 0，应优先调整地基厚度或提示用户台阶无法与固定地基完全一致。

## 楼层关系

EHB 当前没有单独的楼层 Actor；楼层关系由每个元素的 `FloorIndex` 和 `FloorRole` 表达。

- 地基使用 `FloorIndex=0`，`FloorRole=Foundation`。
- 第一层墙、柱使用 `FloorIndex=1`，`FloorRole=FloorBody`；第二层使用 `FloorIndex=2`，以此类推。
- 某一层顶部层板使用该层的 `FloorIndex`，`FloorRole=FloorCeiling`。
- 楼梯使用 `FloorRole=VerticalConnector`，并设置为它起始/主要归属的楼层。
- 如果 AI 编辑了已有元素导致楼层不一致，调用 `SetElementFloorAssignment`。
- 多层生成完成后调用 `GetFloorSummary`，确认每层 `elements` 和 `closedLoops` 都符合预期。
- 多层建筑的 `FloorHeight` 表示相邻楼层结构基准面的距离，通常等于 `WallHeight`。
- 楼板是嵌入下一层内部的构件：顶板 `TopZ` 等于当前层墙顶，楼板厚度向下延伸到当前层空间内。
- 下一层墙底 Z 应等于上一层墙顶 Z，也就是 `FloorIndex=2` 的柱墙底部通常放在 `WallHeight`，不要再额外加楼板厚度，否则会在一层和二层之间产生缝隙。

## 门窗洞口

只有在墙体已经存在之后，才能使用 `AddDoorWindow`。

- `DistanceFromStart` 表示沿墙体中心线，从起点柱子开始测量的距离。
- `Width` 表示门窗洞口水平宽度，单位厘米。
- `Height` 表示门窗洞口垂直高度，单位厘米。
- `SillHeight` 表示洞口底边距离墙体底部或当前楼层地面的高度，单位厘米。门默认使用 `0`，但如果用户要求抬高门或创建高位洞口，可以显式传入大于 `0` 的值。
- `OpeningThickness` 表示洞口厚度，单位厘米。默认传 `0` 时使用墙厚。
- 创建门窗时应一次性传入用户需要的宽、高和距地高度，不要先创建默认门窗再尝试用通用 Actor 缩放修改。
- 室内门是可达性的硬条件，不是装饰：每个非公共封闭房间至少有一个门；公共空间之间可使用较宽门洞或开放口，但不能完全封死。
- 门应放在房间与走廊、门厅、客厅或楼梯间的共享墙上，避开墙角、柱子、楼梯洞口和过短墙段。

## 曲墙

先通过 `ConnectPillars` 创建墙体，再调用 `SetWallCurve` 设置曲墙参数。设置曲墙后再创建房间层板/顶板和屋顶；不要先按直墙填充层板，再把墙改成曲墙。

## 坐标说明

- Unreal 单位为厘米。
- `LocalLocation` 和 `LocalTopPolygon` 都是相对于 `Building` Actor 的局部坐标。
- `Building` Actor 的世界位置就是默认房屋中心点；局部坐标 `(0,0,0)` 对应这个中心点。
- 楼层索引 `0` 预留给地基。
- 楼层索引 `1` 表示第一层普通楼层。
- 对于 `CreateRectangularRoom`，`LocalCenter` 是矩形房间中心；宽度沿 X 轴向两侧展开，深度沿 Y 轴向两侧展开。

## 出错后的恢复流程

如果某一步失败：

1. 停止继续创建新元素。
2. 调用 `GetStatus`。
3. 如果建筑对象存在，调用 `GetBuildingSnapshot`。
4. 继续操作前，优先使用快照中已有对象的引用。
## Existing Building Object Rule

- AI agents must reuse the current existing `EHB_Building` object. Do not create a new root building object for ordinary generation, continuation, regeneration, or modification.
- The first editable target is `GetActiveBuilding()` after `GetStatus()`. Keep passing that same returned `Building` object to all creation and edit tools.
- Do not call `CreateBuilding` as a fallback when status is not ready. If no existing building is selected or uniquely resolvable, stop and ask the user to select or manually create one in the editor.
- If multiple `EHB_Building` actors exist and none is selected, do not guess. Ask the user to select the intended building object.
