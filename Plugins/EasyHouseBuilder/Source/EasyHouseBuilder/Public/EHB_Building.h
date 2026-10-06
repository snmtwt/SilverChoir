// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/EHBBuildingActorBase.h"
#include "EHB_Building.generated.h"

/**
 * 插件默认提供的建筑对象类。
 * 它继承 AEHBBuildingActorBase，保留一个简洁、好识别的默认 Actor 名称；
 * 用户后续可以基于这个类创建蓝图子类，并在项目设置中替换默认建筑对象类。
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "建筑对象", ToolTip = "程序化建筑工具集默认建筑对象。所有墙体、门窗、楼梯等元素都应该在选中的建筑对象中创建和保存。"))
class EASYHOUSEBUILDER_API AEHB_Building : public AEHBBuildingActorBase
{
	GENERATED_BODY()
};
