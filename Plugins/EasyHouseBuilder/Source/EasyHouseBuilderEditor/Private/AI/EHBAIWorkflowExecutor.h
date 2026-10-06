// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AEHBBuildingActorBase;

struct FEHBAIWorkflowDefaults
{
	float WallHeight = 300.0f;
	float WallThickness = 20.0f;
	float FloorSlabThickness = 20.0f;
};

struct FEHBAIWorkflowExecutionResult
{
	bool bSucceeded = false;
	int32 PillarCount = 0;
	int32 WallCount = 0;
	int32 CurvedWallCount = 0;
	int32 DoorWindowCount = 0;
	int32 FloorCount = 0;
	int32 SlabCount = 0;
	int32 RoofCount = 0;
	FText StatusText;
};

class FEHBAIWorkflowExecutor
{
public:
	static FEHBAIWorkflowExecutionResult ExecuteJson(
		AEHBBuildingActorBase* Building,
		const FString& JsonText,
		const FEHBAIWorkflowDefaults& Defaults);
};
