// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cutting/EHBCutTypes.h"

class AEHBElementActorBase;
class UEHBGeneratedMeshComponent;

class EASYHOUSEBUILDER_API FEHBGeneratedMeshCollector
{
public:
	static bool CollectFromActor(
		const AActor* SourceActor,
		const FTransform& TargetLocalToWorld,
		FEHBMeshAggregateData& OutData);

	static bool CollectFromElement(
		const AEHBElementActorBase* SourceElement,
		const FTransform& TargetLocalToWorld,
		FEHBMeshAggregateData& OutData);

	static bool CollectFromComponents(
		const TArray<UEHBGeneratedMeshComponent*>& Components,
		const FTransform& TargetLocalToWorld,
		FEHBMeshAggregateData& OutData,
		const FGuid& SourceElementGuid = FGuid(),
		bool bIncludeHiddenComponents = false);
};
