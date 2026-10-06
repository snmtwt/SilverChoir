// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EHBMeshSampleTemplateMetadata.generated.h"

UENUM(BlueprintType)
enum class EEHBMeshSampleTemplateKind : uint8
{
	Unknown UMETA(DisplayName = "Unknown"),
	WallSurface UMETA(DisplayName = "Wall Surface"),
	Pillar UMETA(DisplayName = "Pillar"),
	Railing UMETA(DisplayName = "Railing"),
	DoorWindow UMETA(DisplayName = "Door/Window"),
	Roof UMETA(DisplayName = "Roof")
};

UENUM(BlueprintType)
enum class EEHBMeshSamplePerformanceClass : uint8
{
	Unknown UMETA(DisplayName = "Unknown"),
	Light UMETA(DisplayName = "Light"),
	Moderate UMETA(DisplayName = "Moderate"),
	Heavy UMETA(DisplayName = "Heavy"),
	Critical UMETA(DisplayName = "Critical")
};

/**
 * Stable metadata shared by all sampled mesh template rows.
 *
 * Data tables remain the user-facing template library, but this metadata lets the
 * tool reason about schema migration, source provenance and expected generation cost.
 */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBMeshSampleTemplateMetadata
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Template")
	FGuid TemplateGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Template", meta = (ClampMin = "1"))
	int32 SchemaVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Template")
	EEHBMeshSampleTemplateKind TemplateKind = EEHBMeshSampleTemplateKind::Unknown;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Template")
	FName TemplateName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Template")
	FString SourceAssetPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Template")
	FString SamplerVersion = TEXT("EHBMeshSampler_v1");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Template")
	EEHBMeshSamplePerformanceClass PerformanceClass = EEHBMeshSamplePerformanceClass::Unknown;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Template", meta = (ClampMin = "0"))
	int32 EstimatedApplyCost = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Template")
	FString Notes;
};
