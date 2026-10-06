// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/EHBGeneratedMeshComponent.h"
#include "Definitions/EHBBuildingTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Sampling/EHBMeshSampleTemplateMetadata.h"
#include "EHBBuildingPerformanceAnalyzer.generated.h"

class AActor;
class AEHBBuildingActorBase;

UENUM(BlueprintType)
enum class EEHBPerformanceBudgetSeverity : uint8
{
	Info UMETA(DisplayName = "信息"),
	Warning UMETA(DisplayName = "警告"),
	Critical UMETA(DisplayName = "严重")
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBBuildingPerformanceBudget
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "性能预算", meta = (DisplayName = "生成网格三角面上限", ClampMin = "0"))
	int32 MaxGeneratedMeshTriangles = 250000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "性能预算", meta = (DisplayName = "生成网格顶点上限", ClampMin = "0"))
	int32 MaxGeneratedMeshVertices = 500000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "性能预算", meta = (DisplayName = "碰撞三角面上限", ClampMin = "0"))
	int32 MaxCollisionTriangles = 100000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "性能预算", meta = (DisplayName = "生成网格组件上限", ClampMin = "0"))
	int32 MaxGeneratedMeshComponents = 512;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "性能预算", meta = (DisplayName = "静态网格组件上限", ClampMin = "0"))
	int32 MaxStaticMeshComponents = 1024;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "性能预算", meta = (DisplayName = "实例化网格实例上限", ClampMin = "0"))
	int32 MaxInstancedMeshInstances = 50000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "性能预算", meta = (DisplayName = "单元素三角面上限", ClampMin = "0"))
	int32 MaxTrianglesPerElement = 50000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "性能预算", meta = (DisplayName = "采样估算开销上限", ClampMin = "0"))
	int32 MaxSampleEstimatedCost = 100000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "性能预算", meta = (DisplayName = "失效关系上限", ClampMin = "0"))
	int32 MaxStaleRelations = 0;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBBuildingPerformanceIssue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	EEHBPerformanceBudgetSeverity Severity = EEHBPerformanceBudgetSeverity::Info;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	FString ActorName;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	FGuid ElementGuid;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBElementPerformanceEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	FString ActorName;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	FGuid ElementGuid;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	EEHBBuildingElementType ElementType = EEHBBuildingElementType::None;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 FloorIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 GeneratedMeshComponentCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	FEHBGeneratedMeshStats GeneratedMeshStats;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 StaticMeshComponentCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 StaticMeshWithAssetCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 InstancedMeshComponentCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 HierarchicalInstancedMeshComponentCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 InstancedMeshInstanceCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 SampledTemplateCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 SampleEstimatedCost = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	EEHBMeshSamplePerformanceClass HighestSamplePerformanceClass = EEHBMeshSamplePerformanceClass::Unknown;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBBuildingPerformanceReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	bool bWithinBudget = true;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	FString BuildingName;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 ActorCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 ElementCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 GeneratedMeshComponentCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	FEHBGeneratedMeshStats GeneratedMeshStats;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 StaticMeshComponentCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 StaticMeshWithAssetCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 InstancedMeshComponentCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 HierarchicalInstancedMeshComponentCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 InstancedMeshInstanceCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 RelationCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 StaleRelationCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 DisabledRelationCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 SampledTemplateCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 SampleEstimatedCost = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 ModerateSampleTemplateCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 HeavySampleTemplateCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 CriticalSampleTemplateCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 RailingPostCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 RailingGateCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	int32 RoofTileInstanceCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	TArray<FEHBElementPerformanceEntry> ElementEntries;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	TArray<FEHBBuildingPerformanceIssue> Issues;
};

UCLASS()
class EASYHOUSEBUILDER_API UEHBBuildingPerformanceAnalyzer : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "EHB|Performance")
	static FEHBElementPerformanceEntry AnalyzeActor(AActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "EHB|Performance")
	static FEHBBuildingPerformanceReport AnalyzeBuilding(
		AEHBBuildingActorBase* Building,
		const FEHBBuildingPerformanceBudget& Budget);

	UFUNCTION(BlueprintPure, Category = "EHB|Performance")
	static FString FormatPerformanceReportSummary(const FEHBBuildingPerformanceReport& Report);

	UFUNCTION(BlueprintPure, Category = "EHB|Performance")
	static bool HasBlockingPerformanceIssues(const FEHBBuildingPerformanceReport& Report);
};
