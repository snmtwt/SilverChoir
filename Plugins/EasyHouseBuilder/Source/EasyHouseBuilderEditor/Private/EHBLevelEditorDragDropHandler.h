// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LevelEditorDragDropHandler.h"

#include "EHBLevelEditorDragDropHandler.generated.h"

UCLASS(Transient)
class UEHBLevelEditorDragDropHandler : public ULevelEditorDragDropHandler
{
	GENERATED_BODY()

public:
	virtual bool PreDropObjectsAtCoordinates(
		int32 MouseX,
		int32 MouseY,
		UWorld* World,
		FViewport* Viewport,
		const TArray<UObject*>& DroppedObjects,
		TArray<AActor*>& OutNewActors) override;
};
