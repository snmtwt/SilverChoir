// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Sampling/EHBMeshSampleTemplateMetadata.h"
#include "EHBMeshSampleValidation.generated.h"

class AEHB_Pillar;
class AEHB_Railing;
class AEHB_Wall;
class UObject;
struct FEHBDoorWindowMeshData;
struct FEHBPillarMeshData;
struct FEHBRailingMeshData;
struct FEHBWallMeshData;

UENUM(BlueprintType)
enum class EEHBMeshSampleValidationSeverity : uint8
{
	Info UMETA(DisplayName = "Info"),
	Warning UMETA(DisplayName = "Warning"),
	Error UMETA(DisplayName = "Error")
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBMeshSampleValidationIssue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Sample Validation")
	EEHBMeshSampleValidationSeverity Severity = EEHBMeshSampleValidationSeverity::Info;

	UPROPERTY(BlueprintReadOnly, Category = "Sample Validation")
	FText Message;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBMeshSampleValidationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Sample Validation")
	bool bValid = true;

	UPROPERTY(BlueprintReadOnly, Category = "Sample Validation")
	EEHBMeshSamplePerformanceClass PerformanceClass = EEHBMeshSamplePerformanceClass::Unknown;

	UPROPERTY(BlueprintReadOnly, Category = "Sample Validation")
	int32 EstimatedApplyCost = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Sample Validation")
	TArray<FEHBMeshSampleValidationIssue> Issues;

	bool HasErrors() const;
	void AddIssue(EEHBMeshSampleValidationSeverity Severity, const FText& Message);
	FText MakeSummaryText() const;
};

class EASYHOUSEBUILDER_API FEHBMeshSampleValidation
{
public:
	static EEHBMeshSamplePerformanceClass ClassifyCost(int32 EstimatedTriangleCost, int32 DisconnectedComponentCount = 1);
	static void InitializeMetadata(
		FEHBMeshSampleTemplateMetadata& Metadata,
		EEHBMeshSampleTemplateKind TemplateKind,
		FName TemplateName,
		UObject* SourceAsset,
		int32 EstimatedApplyCost,
		int32 DisconnectedComponentCount = 1);

	static FEHBMeshSampleValidationResult ValidateWallSurfaceForTarget(
		const FEHBWallMeshData& Row,
		const AEHB_Wall* TargetWall,
		bool bCoverBothSides);

	static FEHBMeshSampleValidationResult ValidatePillarForTarget(
		const FEHBPillarMeshData& Row,
		const AEHB_Pillar* TargetPillar);

	static FEHBMeshSampleValidationResult ValidateRailingForTarget(
		const FEHBRailingMeshData& Row,
		const AEHB_Railing* TargetRailing);

	static FEHBMeshSampleValidationResult ValidateDoorWindowForTarget(
		const FEHBDoorWindowMeshData& Row,
		const AEHB_Wall* TargetWall);
};
