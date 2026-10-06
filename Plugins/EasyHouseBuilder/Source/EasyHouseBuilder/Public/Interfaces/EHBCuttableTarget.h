// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Cutting/EHBCutTypes.h"
#include "EHBCuttableTarget.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UEHBCuttableTarget : public UInterface
{
	GENERATED_BODY()
};

class EASYHOUSEBUILDER_API IEHBCuttableTarget
{
	GENERATED_BODY()

public:
	virtual TArray<FEHBCutOperation>& GetMutableCutOperations()
	{
		static TArray<FEHBCutOperation> EmptyOperations;
		return EmptyOperations;
	}

	virtual const TArray<FEHBCutOperation>& GetCutOperations() const
	{
		static TArray<FEHBCutOperation> EmptyOperations;
		return EmptyOperations;
	}
};
