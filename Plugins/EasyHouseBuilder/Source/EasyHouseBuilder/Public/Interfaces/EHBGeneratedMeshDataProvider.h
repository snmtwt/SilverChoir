// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Cutting/EHBCutTypes.h"
#include "EHBGeneratedMeshDataProvider.generated.h"

class UEHBGeneratedMeshComponent;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UEHBGeneratedMeshDataProvider : public UInterface
{
	GENERATED_BODY()
};

class EASYHOUSEBUILDER_API IEHBGeneratedMeshDataProvider
{
	GENERATED_BODY()

public:
	virtual void GetGeneratedMeshComponents(TArray<UEHBGeneratedMeshComponent*>& OutComponents) const
	{
		OutComponents.Reset();
	}

	virtual bool BuildMeshAggregateData(const FTransform& TargetLocalToWorld, FEHBMeshAggregateData& OutData) const
	{
		OutData.Reset();
		return false;
	}
};
