// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Cutting/EHBCutTypes.h"
#include "EHBCutSourceProvider.generated.h"

class AEHBElementActorBase;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UEHBCutSourceProvider : public UInterface
{
	GENERATED_BODY()
};

class EASYHOUSEBUILDER_API IEHBCutSourceProvider
{
	GENERATED_BODY()

public:
	virtual bool ResolveCutVolumesForTarget(
		const AEHBElementActorBase* TargetElement,
		TArray<FEHBResolvedCutVolume>& OutVolumes) const
	{
		OutVolumes.Reset();
		return false;
	}
};
