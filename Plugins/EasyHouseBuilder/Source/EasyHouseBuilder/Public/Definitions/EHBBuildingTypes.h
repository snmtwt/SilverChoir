// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EHBBuildingTypes.generated.h"

/**
 * 建筑工具中可被创建、保存和编辑的元素类型。
 * 这个枚举会作为数据层与编辑器工具栏之间的稳定桥梁：
 * 左侧工具按钮选择的是某一类编辑面板，建筑对象内部保存的元素也会用同一套类型标记自己。
 */
UENUM(BlueprintType, meta = (DisplayName = "建筑元素类型"))
enum class EEHBBuildingElementType : uint8
{
	None UMETA(DisplayName = "无", ToolTip = "未指定元素类型，通常只用于默认值或无效状态。"),
	MeshSampling UMETA(DisplayName = "网格体采样", ToolTip = "用于从已有网格体或场景对象中采集建筑轮廓、边界或参考数据。"),
	FoundationAndFloor UMETA(DisplayName = "地基/层板", ToolTip = "用于描述建筑的地基、楼层板、平台板等水平承重结构。"),
	Wall UMETA(DisplayName = "墙体", ToolTip = "用于描述墙体路径、高度、厚度以及后续门窗洞口承载关系。"),
	DoorWindow UMETA(DisplayName = "门窗", ToolTip = "用于描述门、窗或其他墙面开口构件。"),
	Railing UMETA(DisplayName = "扶手", ToolTip = "用于描述栏杆、扶手、护栏等沿路径生成的构件。"),
	Roof UMETA(DisplayName = "房顶", ToolTip = "用于描述屋顶轮廓、坡度和屋面构造。"),
	Floor UMETA(DisplayName = "地板", ToolTip = "用于描述楼层表面、地面铺装或室内地板材料。"),
	Stair UMETA(DisplayName = "楼梯", ToolTip = "用于描述楼梯踏步数量、踏步尺寸和楼层连接关系。"),
	Pillar UMETA(DisplayName = "柱子", ToolTip = "用于描述柱子、立柱、墙体端点支撑等竖向构件。")
};

/** Future railing actors share one element category while retaining a precise construction role. */
UENUM(BlueprintType, meta = (DisplayName = "栏杆构件类型"))
enum class EEHBRailingElementKind : uint8
{
	Assembly UMETA(DisplayName = "栏杆组合"),
	Post UMETA(DisplayName = "栏杆柱"),
	Wall UMETA(DisplayName = "栏杆墙"),
	Rail UMETA(DisplayName = "扶手横杆"),
	Infill UMETA(DisplayName = "栏杆填充"),
	Gate UMETA(DisplayName = "围栏门")
};

UENUM(BlueprintType)
enum class EEHBRailingPathMode : uint8
{
	Linear UMETA(DisplayName = "Linear"),
	StairHosted UMETA(DisplayName = "Stair Hosted")
};

UENUM(BlueprintType)
enum class EEHBRailingSide : uint8
{
	Left UMETA(DisplayName = "Left"),
	Right UMETA(DisplayName = "Right")
};

UENUM(BlueprintType)
enum class EEHBRailingPostSpacingMode : uint8
{
	Distance UMETA(DisplayName = "Distance"),
	StepAligned UMETA(DisplayName = "Step Aligned")
};

UENUM(BlueprintType)
enum class EEHBRailingFillMode : uint8
{
	PostsAndRails UMETA(DisplayName = "Posts And Rails"),
	SolidPanel UMETA(DisplayName = "Solid Panel"),
	PostsRailsAndPanel UMETA(DisplayName = "Posts, Rails And Panel")
};

UENUM(BlueprintType)
enum class EEHBRailingGateHingeSide : uint8
{
	Left UMETA(DisplayName = "Left"),
	Right UMETA(DisplayName = "Right")
};

/**
 * 建筑元素在楼层中的角色。
 * FloorIndex 表示它归属哪一层，角色表示它在该层里是主体、顶板还是地基。
 * 约定：地基使用 FloorIndex = 0；一楼墙柱使用 FloorIndex = 1。
 */
UENUM(BlueprintType, meta = (DisplayName = "建筑楼层元素角色"))
enum class EEHBBuildingFloorElementRole : uint8
{
	None UMETA(DisplayName = "未归属", ToolTip = "该元素暂不参与建筑对象的楼层索引。"),
	FloorBody UMETA(DisplayName = "楼层主体", ToolTip = "属于该楼层的主体构件，例如墙体、柱子等。"),
	FloorCeiling UMETA(DisplayName = "楼层顶板", ToolTip = "位于该楼层墙柱顶部、作为这一层楼顶的层板。"),
	Foundation UMETA(DisplayName = "地基", ToolTip = "地基或基础层板。当前约定地基登记在 0 楼。"),
	Roof UMETA(DisplayName = "屋顶", ToolTip = "归属于普通楼层顶部的屋顶。"),
	FloorFinish UMETA(DisplayName = "地板饰面", ToolTip = "位于承重结构顶面的非承重铺装层。"),
	Railing UMETA(DisplayName = "栏杆", ToolTip = "归属于楼层边界的非承重栏杆、栏杆柱或栏杆墙。"),
	HostedElement UMETA(DisplayName = "附属构件", ToolTip = "依附墙体、层板或其他宿主存在的门窗等构件。"),
	VerticalConnector UMETA(DisplayName = "竖向连接", ToolTip = "连接多个楼层的楼梯等构件。")
};

/** 建筑对象按楼层保存的一条元素索引记录。 */
USTRUCT(BlueprintType, meta = (DisplayName = "楼层元素索引记录"))
struct EASYHOUSEBUILDER_API FEHBBuildingFloorElementEntry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "楼层", meta = (DisplayName = "元素唯一标识"))
	FGuid ElementGuid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "楼层", meta = (DisplayName = "元素类型"))
	EEHBBuildingElementType ElementType = EEHBBuildingElementType::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "楼层", meta = (DisplayName = "所属楼层"))
	int32 FloorIndex = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "楼层", meta = (DisplayName = "楼层角色"))
	EEHBBuildingFloorElementRole FloorRole = EEHBBuildingFloorElementRole::None;

	bool operator==(const FEHBBuildingFloorElementEntry& Other) const
	{
		return ElementGuid == Other.ElementGuid;
	}
};

/** TMap 的值类型包装，方便蓝图和序列化稳定保存每层的元素列表。 */
USTRUCT(BlueprintType, meta = (DisplayName = "楼层元素列表"))
struct EASYHOUSEBUILDER_API FEHBBuildingFloorElementList
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "楼层", meta = (DisplayName = "元素列表"))
	TArray<FEHBBuildingFloorElementEntry> Elements;
};

/**
 * 门窗元素内部的具体子类型。
 * 目前先区分门和窗，后续可以继续扩展为洞口、百叶、幕墙模块等。
 */
UENUM(BlueprintType, meta = (DisplayName = "门窗构件类型"))
enum class EEHBDoorWindowElementKind : uint8
{
	Door UMETA(DisplayName = "门", ToolTip = "门类构件，通常落在墙体洞口中并从地面或楼板高度开始。"),
	Window UMETA(DisplayName = "窗", ToolTip = "窗类构件，通常带有窗台高度，并嵌入墙体洞口。")
};

/**
 * 墙体布尔挖洞时读取的门窗洞口数据。
 * 门窗 Actor 会把自身的尺寸、离地高度和变换整理成这个结构，墙体重建时可以统一消费这份数据。
 */
USTRUCT(BlueprintType, meta = (DisplayName = "门窗洞口数据", ToolTip = "墙体布尔挖洞时使用的门窗洞口数据，包含洞口尺寸、底边离地高度、所属墙体和门窗世界变换。"))
struct EASYHOUSEBUILDER_API FEHBDoorWindowOpeningData
{
	GENERATED_BODY()

	/** 当前洞口来自门还是窗。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "洞口", meta = (DisplayName = "构件类型", ToolTip = "表示当前洞口由门还是窗生成。门的底边通常贴地；窗会有窗台高度。"))
	EEHBDoorWindowElementKind Kind = EEHBDoorWindowElementKind::Window;

	/** 洞口水平宽度，单位为厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "洞口", meta = (DisplayName = "洞口宽度", ToolTip = "墙体布尔挖洞时使用的水平宽度，单位厘米。", ClampMin = "1.0", Units = "cm"))
	float Width = 120.0f;

	/** 洞口垂直高度，单位为厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "洞口", meta = (DisplayName = "洞口高度", ToolTip = "墙体布尔挖洞时使用的垂直高度，单位厘米。", ClampMin = "1.0", Units = "cm"))
	float Height = 150.0f;

	/** 洞口在墙体厚度方向上的厚度，单位为厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "洞口", meta = (DisplayName = "洞口厚度", ToolTip = "门窗构件或布尔挖洞盒体在墙体厚度方向上的尺寸，单位厘米。", ClampMin = "1.0", Units = "cm"))
	float Thickness = 10.0f;

	/** 洞口底边相对墙体底部或当前楼层地面的高度，单位为厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "洞口", meta = (DisplayName = "底边离地高度", ToolTip = "洞口底边相对墙体底部或当前楼层地面的高度。门通常为 0；窗通常大于 0。", ClampMin = "0.0", Units = "cm"))
	float BottomHeight = 90.0f;

	/** 该洞口依附的墙体唯一标识。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "连接", meta = (DisplayName = "所属墙体标识", ToolTip = "门窗依附的墙体唯一标识。墙体重建洞口时可用它过滤属于自己的门窗。"))
	FGuid OwningWallGuid;

	/** 门窗 Actor 的世界变换，墙体可将其转换到自身局部空间后计算洞口位置。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "连接", meta = (DisplayName = "世界变换", ToolTip = "门窗 Actor 当前的世界变换。墙体做布尔挖洞时通常会先把它转换到墙体局部空间。"))
	FTransform WorldTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Opening", meta = (DisplayName = "Local Opening Outline Points", ToolTip = "Closed spline outline in door/window local space. All points are constrained to Y = 0."))
	TArray<FVector> LocalOutlinePoints;
};

/**
 * 墙体保存的门窗连接记录。
 * 当前阶段只记录门窗和墙体的相互关系，以及门窗沿墙体起点到终点方向的距离；后续墙体挖洞会直接读取这些记录。
 */
USTRUCT(BlueprintType, meta = (DisplayName = "墙体门窗连接数据", ToolTip = "墙体内部保存的一条门窗连接记录。它记录门窗唯一标识、洞口尺寸、离墙起点距离和门窗相对墙体的局部变换，便于保存加载和后续布尔挖洞。"))
struct EASYHOUSEBUILDER_API FEHBWallDoorWindowConnection
{
	GENERATED_BODY()

	/** 连接到该墙体上的门窗元素唯一标识。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "门窗连接", meta = (DisplayName = "门窗标识", ToolTip = "连接到当前墙体上的门窗 Actor 唯一标识。保存后可以用它重新找回对应门窗。"))
	FGuid DoorWindowGuid;

	/** 当前连接记录对应的是门还是窗。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "门窗连接", meta = (DisplayName = "构件类型", ToolTip = "表示这条连接记录来自门还是窗。门通常底边贴地；窗通常带有窗台高度。"))
	EEHBDoorWindowElementKind Kind = EEHBDoorWindowElementKind::Window;

	/** 门窗中心投影到墙体中心线后，距离墙体本地起点的长度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "门窗连接", meta = (DisplayName = "距起点距离", ToolTip = "门窗位置沿墙体起点到终点方向投影后，距离墙体起点的长度。后续保存加载和墙体挖洞都会用它恢复门窗沿墙位置。", ClampMin = "0.0", Units = "cm"))
	float DistanceFromStart = 0.0f;

	/** 洞口底边相对墙体底部的高度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "洞口", meta = (DisplayName = "底边离地高度", ToolTip = "洞口底边相对墙体底部或当前楼层地面的高度。门通常为 0；窗通常大于 0。", ClampMin = "0.0", Units = "cm"))
	float BottomHeight = 90.0f;

	/** 洞口水平宽度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "洞口", meta = (DisplayName = "洞口宽度", ToolTip = "墙体后续布尔挖洞时使用的洞口水平宽度，单位厘米。", ClampMin = "1.0", Units = "cm"))
	float OpeningWidth = 120.0f;

	/** 洞口垂直高度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "洞口", meta = (DisplayName = "洞口高度", ToolTip = "墙体后续布尔挖洞时使用的洞口垂直高度，单位厘米。", ClampMin = "1.0", Units = "cm"))
	float OpeningHeight = 150.0f;

	/** 洞口在墙体厚度方向上的厚度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "洞口", meta = (DisplayName = "洞口厚度", ToolTip = "墙体后续布尔挖洞时使用的洞口厚度，单位厘米。", ClampMin = "1.0", Units = "cm"))
	float OpeningThickness = 10.0f;

	/** 门窗 Actor 相对当前墙体 Actor 的局部变换。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "门窗连接", meta = (DisplayName = "相对墙体变换", ToolTip = "门窗 Actor 相对当前墙体 Actor 的局部变换。它用于调试、保存和后续更精确地重建洞口位置。"))
	FTransform DoorWindowLocalToWall = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Opening", meta = (DisplayName = "Local Opening Outline Points"))
	TArray<FVector> LocalOutlinePoints;
};

/**
 * 元素的轻量引用句柄。
 * 后续 UI、命令、撤销重做或保存数据可以只传递这个句柄，而不直接持有 UObject 指针。
 */
USTRUCT(BlueprintType, meta = (DisplayName = "建筑元素句柄", ToolTip = "用于在 UI、保存数据或工具命令中轻量引用一个建筑元素，而不直接持有 UObject 指针。"))
struct EASYHOUSEBUILDER_API FEHBBuildingElementHandle
{
	GENERATED_BODY()

	/** 元素的唯一 ID，用于在建筑对象内部稳定查找对应元素。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "元素", meta = (DisplayName = "元素唯一标识", ToolTip = "每个建筑元素都会拥有一个全局唯一 ID，用于保存、查找和跨面板引用。"))
	FGuid ElementGuid;

	/** 元素类型，帮助调用方知道这个句柄指向哪一类建筑元素。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "元素", meta = (DisplayName = "元素类型", ToolTip = "句柄对应的建筑元素类型。它会与元素对象内部的类型保持一致。"))
	EEHBBuildingElementType ElementType = EEHBBuildingElementType::None;

	/** 判断句柄是否同时拥有有效 ID 和有效元素类型。 */
	bool IsValid() const
	{
		return ElementGuid.IsValid() && ElementType != EEHBBuildingElementType::None;
	}
};
